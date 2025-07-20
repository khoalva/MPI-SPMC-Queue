#define _POSIX_C_SOURCE 199309L
#define _GNU_SOURCE  // Add this for broader usleep support
#include "spmc_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>



int spmc_queue_init(spmc_queue_t *queue, int argc, char *argv[]) {

    if (!queue) return MPI_ERR_ARG;
    
    // Initialize MPI using the new library
    MPI_TRY(mpi_init(argc, argv, &queue->mpi_ctx));
    
    // Check minimum number of processes
    if (mpi_get_size(&queue->mpi_ctx) < 2) {
        fprintf(stderr, "At least two processes are required\n");
        mpi_finalize();
        return MPI_ERR_OTHER;
    }

    // Allocate memory for cells
    queue->size = MAX_QUEUE_SIZE;
    queue->cells = malloc(queue->size * sizeof(spmc_cell_t));
    if (!queue->cells) {
        fprintf(stderr, "Failed to allocate memory for queue cells\n");
        mpi_finalize();
        return MPI_ERR_OTHER;
    }
    // Initialize queue metadata
    queue->head = 0;
    queue->tail = 0;
    // Initialize cells
    if(mpi_is_root(&queue->mpi_ctx)) {
        for (int i = 0; i < queue->size; i++) {
            queue->cells[i].rank = EMPTY_CELL; // Mark as empty
            queue->cells[i].gap = 0; // No gap initially
            queue->cells[i].data = 0; // No data
        }
    }

    // Create MPI window for cells
    MPI_TRY(mpi_win_create(queue->cells,
                           mpi_is_root(&queue->mpi_ctx) ? queue->size * sizeof(spmc_cell_t) : 0,
                           sizeof(spmc_cell_t), queue->mpi_ctx.comm, &queue->win_cells));
    
    // Create MPI window for head
    MPI_TRY(mpi_win_create(&queue->head,
                           mpi_is_root(&queue->mpi_ctx) ? sizeof(int) : 0,
                           sizeof(int), queue->mpi_ctx.comm, &queue->win_head));

    // Lock all windows for passive target synchronization
    if (mpi_is_root(&queue->mpi_ctx)) {
        mpi_window_t windows[] = {queue->win_cells, queue->win_head};
        MPI_TRY(mpi_win_lock_all_multiple(windows, 2));
    }
    
    printf("SPMC Queue initialized successfully on rank %d/%d\n", 
           mpi_get_rank(&queue->mpi_ctx), mpi_get_size(&queue->mpi_ctx));
    
    return MPI_SUCCESS;
}


void spmc_queue_destroy(spmc_queue_t *queue) {
    if (!queue) return;

    // Chỉ root mới unlock_all
    if (mpi_is_root(&queue->mpi_ctx)) {
        mpi_window_t windows[] = {queue->win_cells, queue->win_head};
        mpi_win_unlock_all_multiple(windows, 2);
    }

    mpi_win_destroy(&queue->win_cells);

    mpi_finalize();
    printf("SPMC Queue destroyed on rank %d\n", mpi_get_rank(&queue->mpi_ctx));
}


int spmc_queue_enqueue(spmc_queue_t *queue, int item) {
    if (!queue) return -1;

    // Only producer should enqueue
    if (!mpi_is_root(&queue->mpi_ctx)) return -1;

    int current_tail = queue->tail;
    int next_tail = (current_tail + 1) % queue->size;

    int head = mpi_get(&queue->head, sizeof(int), MPI_INT, 0, 0, &queue->win_head);
    // Check if queue is full
    if (next_tail == head) {
        printf("[ENQUEUE][rank %d] Queue is full! tail=%d, head=%d\n", mpi_get_rank(&queue->mpi_ctx), current_tail, queue->head);
        return -1; // Queue is full
    }

    spmc_cell_t c = mpi_get(queue->cells, sizeof(spmc_cell_t), MPI_BYTE, 0, 
                                sizeof(queue_t) + current_tail * sizeof(spmc_cell_t), &queue->win_cells);

    if (c.rank != EMPTY_CELL) {
        printf("[ENQUEUE][rank %d] Cell already occupied at pos %d\n", mpi_get_rank(&queue->mpi_ctx), current_tail);
        c.gap = current_tail; // Update gap to current tail
        MPI_TRY(mpi_put(&c, sizeof(spmc_cell_t), MPI_BYTE, 0, 
                sizeof(queue->cells) + current_tail * sizeof(spmc_cell_t), &queue->win_cells));
        return -1; // Cell already occupied
    } else {
        // Mark cell as occupied
        c.rank = queue->tail;
        c.data = item;
        c.gap = 0;
        // Update the cell in producer's memory
        MPI_TRY(mpi_put(&c, sizeof(spmc_cell_t), MPI_BYTE, 0, 
                sizeof(queue->cells) + current_tail * sizeof(spmc_cell_t), &queue->win_cells));
    }
    queue->tail = next_tail;
    // Log enqueue action
    printf("[ENQUEUE][rank %d] Enqueued item: %d at pos %d | new tail=%d, head=%d\n", mpi_get_rank(&queue->mpi_ctx), item, current_tail, queue->tail, queue->head);

    mpi_win_flush(0, &queue->win_cells);
    return MPI_SUCCESS;
}


int spmc_queue_dequeue(spmc_queue_t *queue) {
    if (!queue) return -1;

    int one = 1;
    int rank;
    MPI_TRY(mpi_fetch_and_op(&one, &rank, MPI_INT, 0, 0, MPI_SUM, &queue->win_head));
    
    int c = mpi_get(&queue->cells, sizeof(spmc_cell_t), MPI_BYTE, 0, 
                                sizeof(queue_t) + rank * sizeof(spmc_cell_t), &queue->win_cells);
    if (c.rank == rank) {
        // Cell is occupied, dequeue it
        int item = c.data;
        c.rank = EMPTY_CELL; // Mark as empty
        c.gap++; // Increment gap
        MPI_TRY(mpi_put(&c, sizeof(spmc_cell_t), MPI_BYTE, 0, 
                sizeof(queue->cells) + rank * sizeof(spmc_cell_t), &queue->win_cells));
        
        printf("[DEQUEUE][rank %d] Dequeued item: %d at pos %d | head=%d, tail=%d\n", mpi_get_rank(&queue->mpi_ctx), item, rank, queue->head, queue->tail);
        mpi_win_flush(0, &queue->win_cells);
        return MPI_SUCCESS;
    } 
    else {
        printf("[DEQUEUE][rank %d] Cell already empty at pos %d\n", mpi_get_rank(&queue->mpi_ctx), rank);
        return -1; // Cell was already empty
    }
}

int spmc_queue_is_enqueuer(spmc_queue_t *queue) {
    return queue && mpi_is_root(&queue->mpi_ctx);
}

void spmc_queue_print_stats(spmc_queue_t *queue) {
    printf("Queue size: %d, head: %d, tail: %d\n", queue->q.size, queue->q.head, queue->q.tail);
}

// Trả về tổng số byte mà queue cấp phát cho dữ liệu (metadata + cells)
size_t spmc_queue_get_capacity_bytes(const spmc_queue_t *queue) {
    if (!queue) return 0;
    return sizeof(queue_t) + queue->q.size * sizeof(spmc_cell_t);
}

// ==== END: Required API for benchmark compatibility ====