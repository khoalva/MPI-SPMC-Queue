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

    int size = 128;
    if (argc > 1) {
        int parsed = atoi(argv[1]);
        if (parsed > 0 && parsed <= MAX_QUEUE_SIZE) size = parsed;
    }

    // Cấp phát block liên tục cho metadata và cells
    size_t meta_size = sizeof(queue_t);
    size_t cells_size = size * sizeof(spmc_cell_t);
    size_t total_size = meta_size + cells_size;
    void* block = NULL;
    if (mpi_is_root(&queue->mpi_ctx)) {
        block = malloc(total_size);
        memset(block, 0, total_size);
        // Gán metadata
        queue_t* meta = (queue_t*)block;
        meta->size = size;
        meta->head = 0;
        meta->tail = 0;
        meta->lastItemDequeued = -1;
        meta->cells = (spmc_cell_t*)((char*)block + meta_size);
        for (int i = 0; i < size; ++i) {
            meta->cells[i].rank = EMPTY_CELL;
            meta->cells[i].gap = 0;
            meta->cells[i].data = EMPTY_CELL;
        }
        // Gán lại cho queue->q
        queue->q = *meta;
        queue->q.cells = meta->cells;
    } else {
        block = NULL;
    }

    // Tạo MPI window cho block
    MPI_TRY(mpi_win_create(block, mpi_is_root(&queue->mpi_ctx) ? total_size : 0, 1, queue->mpi_ctx.comm, &queue->win_queue));

    mpi_barrier(queue->mpi_ctx.comm); // Ensure all processes are synchronized after window creation

    // Lock all windows for passive target synchronization
    if (mpi_is_root(&queue->mpi_ctx)) {
        mpi_window_t windows[] = {queue->win_queue};
        MPI_TRY(mpi_win_lock_all_multiple(windows, 1));
    }
    
    printf("SPMC Queue initialized successfully on rank %d/%d\n", 
           mpi_get_rank(&queue->mpi_ctx), mpi_get_size(&queue->mpi_ctx));
    
    return MPI_SUCCESS;
}


void spmc_queue_destroy(spmc_queue_t *queue) {
    if (!queue) return;

    // Chỉ root mới unlock_all
    if (mpi_is_root(&queue->mpi_ctx)) {
        mpi_window_t windows[] = {queue->win_queue};
        mpi_win_unlock_all_multiple(windows, 1);
    }

    mpi_win_destroy(&queue->win_queue);
    // Không cần free queue->q.cells vì đã free block ở root, nếu cần thì chỉ root free block
    mpi_finalize();
    printf("SPMC Queue destroyed on rank %d\n", mpi_get_rank(&queue->mpi_ctx));
}


int spmc_queue_enqueue(spmc_queue_t *queue, int item) {
    if (!queue) return -1;

    // Only producer should enqueue
    if (!mpi_is_root(&queue->mpi_ctx)) return -1;
    
    // Start passive target epoch for producer
    // MPI_TRY(mpi_win_lock(MPI_LOCK_EXCLUSIVE, 0, 0, &queue->win_queue));

    int current_tail = queue->q.tail;
    int next_tail = (current_tail + 1) % queue->q.size;
    
    // Check if queue is full
    if (next_tail == queue->q.head) {
        // MPI_TRY(mpi_win_unlock(0, &queue->win_queue));

        return -1; // Queue is full
    }
    
    // Fill the cell (producer can access directly)
    queue->q.cells[current_tail].rank = mpi_get_rank(&queue->mpi_ctx);
    queue->q.cells[current_tail].gap = 0;
    queue->q.cells[current_tail].data = item;
    
    // Update tail pointer
    queue->q.tail = next_tail;

    // Đồng bộ metadata lên window để consumer thấy được trạng thái mới nhất
    mpi_put(&queue->q, sizeof(queue_t), MPI_BYTE, 0, 0, &queue->win_queue);
    mpi_win_flush(0, &queue->win_queue);

    return MPI_SUCCESS;
}


int spmc_queue_dequeue(spmc_queue_t *queue) {
    if (!queue) return -1;

    // Start passive target epoch for this consumer
    MPI_TRY(mpi_win_lock(MPI_LOCK_SHARED, 0, 0, &queue->win_queue));
    
    // For consumers, we need to access queue data through MPI window
    queue_t local_queue;
    if (!mpi_is_root(&queue->mpi_ctx)) {
        // Consumer: Get queue metadata from producer (rank 0)
        int result = mpi_get(&local_queue, sizeof(queue_t), MPI_BYTE, 0, 0, &queue->win_queue);
        if (result != MPI_SUCCESS) {
            mpi_win_unlock(0, &queue->win_queue);
            return -1;
        }
    }
    
    // Atomic fetch-and-op để lấy và tăng head
    int one = 1;
    int my_head = -1;
    int queue_size = local_queue.size;
    if (!mpi_is_root(&queue->mpi_ctx)) {
        mpi_fetch_and_op(&one, &my_head, MPI_INT, 0, offsetof(queue_t, head), MPI_SUM, &queue->win_queue);
        mpi_win_flush(0, &queue->win_queue);
        // Lấy lại local_queue.tail để kiểm tra empty
        int tail = 0;
        int result = mpi_get(&tail, sizeof(int), MPI_BYTE, 0, offsetof(queue_t, tail), &queue->win_queue);
        if (result != MPI_SUCCESS) {
            mpi_win_unlock(0, &queue->win_queue);
            return -1;
        }
        // Wrap-around chỉ số head
        my_head = my_head % queue_size;
        if (my_head == tail) {
            mpi_win_unlock(0, &queue->win_queue);
            return -1; // Queue is empty
        }
    } else {
        // Producer không dequeue
        mpi_win_unlock(0, &queue->win_queue);
        return -1;
    }

    // For consumers, get the cell data from producer's memory
    spmc_cell_t cell;
    size_t cell_offset = sizeof(queue_t) + my_head * sizeof(spmc_cell_t);
    int result = mpi_get(&cell, sizeof(spmc_cell_t), MPI_BYTE, 0, cell_offset, &queue->win_queue);
    if (result != MPI_SUCCESS) {
        mpi_win_unlock(0, &queue->win_queue);
        return -1;
    }

    // Check if this item was already dequeued
    if (cell.rank == EMPTY_CELL) {
        mpi_win_unlock(0, &queue->win_queue);
        return -1;
    }

    // Copy the data
    int item = cell.data;

    // Mark cell as consumed and update queue state
    cell.rank = EMPTY_CELL;
    cell.gap++;

    // Update the cell in producer's memory
    result = mpi_put(&cell, sizeof(spmc_cell_t), MPI_BYTE, 0, cell_offset, &queue->win_queue);
    if (result != MPI_SUCCESS) {
        mpi_win_unlock(0, &queue->win_queue);
        return false;
    }

    // End passive target epoch
    mpi_win_unlock(0, &queue->win_queue);
    return item;
}

int spmc_queue_is_enqueuer(spmc_queue_t *queue) {
    return queue && mpi_is_root(&queue->mpi_ctx);
}

void spmc_queue_print_stats(spmc_queue_t *queue) {
    printf("Queue size: %d, head: %d, tail: %d\n", queue->q.size, queue->q.head, queue->q.tail);
}

// ==== END: Required API for benchmark compatibility ====