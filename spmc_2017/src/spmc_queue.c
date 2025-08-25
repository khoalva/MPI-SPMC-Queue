#define _POSIX_C_SOURCE 199309L
#define _GNU_SOURCE  // Add this for broader usleep support
#include "spmc_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/**
 * @brief Initializes the SPMC queue and necessary MPI resources.
 */
int spmc_queue_init(spmc_queue_t *queue, int argc, char *argv[]) {
    if (!queue) return -1;

    MPI_TRY(mpi_init(argc, argv, &queue->mpi_ctx));

    if (mpi_get_size(&queue->mpi_ctx) < 2) {
        fprintf(stderr, "At least two processes (1 producer, 1+ consumer) are required.\n");
        mpi_finalize();
        return -1;
    }

    queue->size = MAX_QUEUE_SIZE;
    int is_producer = (mpi_get_rank(&queue->mpi_ctx) == 0);

    // Only the producer (rank 0) allocates and initializes the queue memory.
    if (is_producer) {
        queue->cells = malloc(queue->size * sizeof(spmc_cell_t));
        if (!queue->cells) {
            fprintf(stderr, "Failed to allocate memory for queue cells.\n");
            mpi_finalize();
            return -1;
        }
        queue->head = 0;
        queue->tail = 0;
        for (int i = 0; i < queue->size; i++) {
            queue->cells[i].rank = EMPTY_CELL;
            queue->cells[i].gap = 0;
            queue->cells[i].data = 0;
        }
    } else {
        // Consumers do not allocate the main memory.
        queue->cells = NULL;
        queue->head = 0;
        queue->tail = 0;
    }

    // Create MPI windows for one-sided access. The size is 0 for consumers.
    size_t cells_size = is_producer ? queue->size * sizeof(spmc_cell_t) : 0;
    size_t head_size = is_producer ? sizeof(int) : 0;
    MPI_TRY(mpi_win_create(queue->cells, cells_size, 1, queue->mpi_ctx.comm, &queue->win_cells));
    MPI_TRY(mpi_win_create(&queue->head, head_size, sizeof(int), queue->mpi_ctx.comm, &queue->win_head));

    // Lock all windows to enable passive target synchronization, which allows
    // consumers to perform RMA operations without explicit calls from the producer.
    mpi_window_t windows[] = {queue->win_cells, queue->win_head};
    MPI_TRY(mpi_win_lock_all_multiple(windows, 2));

    printf("SPMC Queue initialized on rank %d/%d\n",
           mpi_get_rank(&queue->mpi_ctx), mpi_get_size(&queue->mpi_ctx));

    return MPI_SUCCESS;
}

/**
 * @brief Destroys the queue and frees all associated resources.
 */
void spmc_queue_destroy(spmc_queue_t *queue) {
    if (!queue) return;

    mpi_window_t windows[] = {queue->win_cells, queue->win_head};
    mpi_win_unlock_all_multiple(windows, 2);

    mpi_win_destroy(&queue->win_cells);
    mpi_win_destroy(&queue->win_head);

    // The producer frees the memory it allocated.
    if (spmc_queue_is_enqueuer(queue) && queue->cells) {
        free(queue->cells);
        queue->cells = NULL;
    }
    
    printf("SPMC Queue destroyed on rank %d\n", mpi_get_rank(&queue->mpi_ctx));
    mpi_finalize();
}

/**
 * @brief Enqueues an item using FFQ logic. Only the producer (rank 0) should call this.
 * @return MPI_SUCCESS on success, or -1 if the queue is full or contended.
 */
int spmc_queue_enqueue(spmc_queue_t *queue, int value) {
    if (!spmc_queue_is_enqueuer(queue)) return -1;
    bool success = false;
    const int MAX_POLL_ATTEMPTS = 20;
    int poll_attempts = 0;
    while(!success && poll_attempts < MAX_POLL_ATTEMPTS) {
            
        int current_tail_val = queue->tail;
        // int head_val;

        // // First, do a robust check for fullness to prevent tail from lapping head indefinitely.
        // MPI_TRY(mpi_get(&head_val, sizeof(int), MPI_BYTE, 0, 0, &queue->win_head));

        // if ((current_tail_val - head_val) >= queue->size) {
        //     printf("[ENQUEUE][rank %d] Queue is full! tail=%d, head=%d\n", mpi_get_rank(&queue->mpi_ctx), current_tail_val, head_val);
        //     return -1;
        // }

        int pos = current_tail_val % queue->size;
        spmc_cell_t cell;

        // Read the current state of the target cell from the MPI window.
        MPI_TRY(mpi_get(&cell, sizeof(spmc_cell_t), MPI_BYTE, 0, pos * sizeof(spmc_cell_t), &queue->win_cells));

        // Check if the cell is used (rank >= 0).
        if (cell.rank != EMPTY_CELL) {
            // The cell is currently occupied by a previous value that a consumer hasn't processed yet.
            // As per the FFQ algorithm, mark a "gap" to indicate this slot was skipped.
            printf("[ENQUEUE][rank %d] Contention at pos %d. Cell is not empty.\n", mpi_get_rank(&queue->mpi_ctx), pos);
            cell.gap = current_tail_val;
            MPI_TRY(mpi_put(&cell, sizeof(spmc_cell_t), MPI_BYTE, 0, pos * sizeof(spmc_cell_t), &queue->win_cells));
            queue->tail++;
            poll_attempts++;
        } else {
            // The cell is empty, so we can claim it.
            cell.data = value;
            cell.rank = current_tail_val; // Claim the cell by setting its rank to the current tail value.

            // Put the new cell data into shared memory.
            MPI_TRY(mpi_put(&cell, sizeof(spmc_cell_t), MPI_BYTE, 0, pos * sizeof(spmc_cell_t), &queue->win_cells));

            // IMPORTANT: The local tail is only incremented after the data is successfully written.
            queue->tail = current_tail_val + 1;

            printf("[ENQUEUE][rank %d] Enqueued item: %d at pos %d | new_tail=%d\n",
                mpi_get_rank(&queue->mpi_ctx), value, pos, queue->tail);

            success = true;
        }
    }
    if(!success){
        printf("[ENQUEUE][rank %d] Failed to enqueue after %d attempts. Queue might be full or contended.\n", mpi_get_rank(&queue->mpi_ctx), poll_attempts);
        fflush(stdout);
        return -1;
    }
    return MPI_SUCCESS;
}
/**
 * @brief Dequeues an item using FFQ logic. This function will not loop indefinitely.
 * @return MPI_SUCCESS on success, -1 on failure (e.g., queue empty, contention, or timeout).
 */
int spmc_queue_dequeue(spmc_queue_t *queue) {
    if (spmc_queue_is_enqueuer(queue)) return -1;

    int my_rank;
    int one = 1;
    const int MAX_POLL_ATTEMPTS = 200; // Poll for ~2ms before giving up.

    // Atomically get a rank to process. This is our one "ticket" for this attempt.
    MPI_TRY(mpi_fetch_and_op(&one, &my_rank, MPI_INT, 0, 0, MPI_SUM, &queue->win_head));

    int pos = my_rank % queue->size;
    MPI_Aint disp = pos * sizeof(spmc_cell_t);
    int poll_attempts = 0;

    // Poll the specific cell for a limited time.
    while (poll_attempts < MAX_POLL_ATTEMPTS) {
        spmc_cell_t c;
        MPI_TRY(mpi_get(&c, sizeof(spmc_cell_t), MPI_BYTE, 0, disp, &queue->win_cells));

        // Case 1: The cell's rank matches our rank. It's ready for us to claim.
        if (c.rank == my_rank) {
            // Attempt to claim the cell via Compare-and-Swap.
            int empty_val = EMPTY_CELL;
            int compare_val = my_rank;
            int result_val;
            MPI_Aint rank_disp = disp + offsetof(spmc_cell_t, rank);

            // Assuming mpi_compare_and_swap exists and maps to MPI_Compare_and_swap.
            MPI_TRY(mpi_compare_and_swap(&empty_val, &compare_val, &result_val, MPI_INT, 0, rank_disp, &queue->win_cells));

            // If the value before the swap was our rank, we successfully claimed it.
            if (result_val == my_rank) {
                printf("[DEQUEUE][rank %d] Dequeued item: %d at pos %d\n", mpi_get_rank(&queue->mpi_ctx), c.data, pos);
                return MPI_SUCCESS;
            } else {
                // We lost a race to another consumer for this specific item.
                return -1;
            }
        }
        // Case 2: The cell has been skipped by the producer (a "gap").
        else if (c.gap >= my_rank && c.rank != my_rank) {
            // Still have item to dequeue so just try again
            return -1;
        }
        // Case 3: The cell is not ready yet. Wait and poll again.
        else {
            usleep(10); // Back off to prevent busy-spinning.
            poll_attempts++;
        }
    }

    // If we exit the loop, it means we timed out waiting for the producer. The queue is likely empty.
    return -1;
}

/**
 * @brief Prints statistics about the queue.
 */
void spmc_queue_print_stats(spmc_queue_t *queue) {
    if (spmc_queue_is_enqueuer(queue)) {
        printf("Queue Stats -> size: %d, head: %d, tail: %d\n", queue->size, queue->head, queue->tail);
    }
}

/**
 * @brief Checks if the current process is the producer (rank 0).
 */
int spmc_queue_is_enqueuer(spmc_queue_t *queue) {
    return queue && mpi_is_root(&queue->mpi_ctx);
}

/**
 * @brief Returns the total bytes allocated by the queue.
 */
size_t spmc_queue_get_capacity_bytes(spmc_queue_t *queue) {
    if (!queue) return 0;
    if (spmc_queue_is_enqueuer(queue)) {
        return queue->size * sizeof(spmc_cell_t) + sizeof(queue->head) + sizeof(queue->tail);
    }
    return 0;
}