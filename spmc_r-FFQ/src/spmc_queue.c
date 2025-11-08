#define _POSIX_C_SOURCE 199309L
#define _GNU_SOURCE  // Add this for broader usleep support
#include "spmc_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/**
 * @brief Initializes the SPMC queue and necessary MPI resources with configurable queue owner.
 */
int spmc_queue_init_with_queue_owner(spmc_queue_t *queue, int argc, char *argv[], int queue_owner_rank) {
    if (!queue) return -1;

    MPI_TRY(mpi_init(argc, argv, &queue->mpi_ctx));

    if (mpi_get_size(&queue->mpi_ctx) < 2) {
        // fprintf(stderr, "At least two processes (1 producer, 1+ consumer) are required.\n");
        mpi_finalize();
        return -1;
    }

    int rank = mpi_get_rank(&queue->mpi_ctx);
    int size = mpi_get_size(&queue->mpi_ctx);
    
    // Validate queue owner rank
    if (queue_owner_rank < 0 || queue_owner_rank >= size) {
        if (rank == 0) {
            fprintf(stderr, "Invalid queue owner rank %d (must be 0-%d)\n", queue_owner_rank, size-1);
        }
        mpi_finalize();
        return -1;
    }
    
    // Store queue owner rank
    queue->queue_owner_rank = queue_owner_rank;
    queue->size = MAX_QUEUE_SIZE;
    
    // Producer is always rank 0
    int is_producer = (rank == 0);
    
    // Queue owner is the one who allocates memory
    int is_queue_owner = (rank == queue_owner_rank);

    // Only the queue owner allocates and initializes the queue memory.
    if (is_queue_owner) {
        queue->cells = malloc(queue->size * sizeof(spmc_cell_t));
        if (!queue->cells) {
            // fprintf(stderr, "Failed to allocate memory for queue cells.\n");
            mpi_finalize();
            return -1;
        }
        queue->head = 0;
        queue->tail = 0;
        for (int i = 0; i < queue->size; i++) {
            queue->cells[i].rank = EMPTY_CELL;
            queue->cells[i].gap = -1;
            queue->cells[i].data = 0;
        }
    } else {
        // Non-queue-owner nodes do not allocate the main memory.
        queue->cells = NULL;
        queue->head = 0;
        queue->tail = 0;
    }

    // Create MPI windows for one-sided access. The size is 0 for non-queue-owner.
    size_t cells_size = is_queue_owner ? queue->size * sizeof(spmc_cell_t) : 0;
    size_t head_size = is_queue_owner ? sizeof(int) : 0;
    MPI_TRY(mpi_win_create(queue->cells, cells_size, 1, queue->mpi_ctx.comm, &queue->win_cells));
    MPI_TRY(mpi_win_create(&queue->head, head_size, sizeof(int), queue->mpi_ctx.comm, &queue->win_head));

    // Lock all windows to enable passive target synchronization, which allows
    // consumers to perform RMA operations without explicit calls from the producer.
    mpi_window_t windows[] = {queue->win_cells, queue->win_head};
    MPI_TRY(mpi_win_lock_all_multiple(windows, 2));

    printf("SPMC Queue initialized on rank %d/%d (queue_owner=%d)\n",
           rank, size, queue_owner_rank);

    return MPI_SUCCESS;
}

// Backward compatibility: default queue owner at rank 0
int spmc_queue_init(spmc_queue_t *queue, int argc, char *argv[]) {
    return spmc_queue_init_with_queue_owner(queue, argc, argv, 0);
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

    // The queue owner frees the memory it allocated.
    int rank = mpi_get_rank(&queue->mpi_ctx);
    if (rank == queue->queue_owner_rank && queue->cells) {
        free(queue->cells);
        queue->cells = NULL;
    }
    
    // printf("SPMC Queue destroyed on rank %d\n", mpi_get_rank(&queue->mpi_ctx));
    mpi_finalize();
}

/**
 * @brief Enqueues an item using FFQ logic. Only the producer (rank 0) should call this.
 * @return MPI_SUCCESS on success, or -1 if the queue is full or contended.
 */
int spmc_queue_enqueue(spmc_queue_t *queue, int value) {
    if (!spmc_queue_is_enqueuer(queue)) return -1;
    
    int target_rank = queue->queue_owner_rank;  // Target the queue owner
    bool success = false;
    
    // Line 3: while ¬success do
    while (!success) {
        // Line 4: rank = AtomicRead(cells[tail(mod N)].rank)
        int pos = queue->tail % queue->size;
        MPI_Aint rank_disp = pos * sizeof(spmc_cell_t) + offsetof(spmc_cell_t, rank);
        
        // AtomicRead: MPI_Fetch_and_op with MPI_NO_OP
        int cell_rank;
        int no_op_val = 0;
        MPI_TRY(mpi_fetch_and_op(&no_op_val, &cell_rank, MPI_INT, target_rank, rank_disp, MPI_NO_OP, &queue->win_cells));
        
        // Line 5: if rank ≥ 0 then
        if (cell_rank >= 0) {
            // Line 6: AtomicWrite(cells[tail(mod N)].gap, tail)
            // AtomicWrite: MPI_Accumulate with MPI_REPLACE
            MPI_Aint gap_disp = pos * sizeof(spmc_cell_t) + offsetof(spmc_cell_t, gap);
            int tail_val = queue->tail;
            MPI_TRY(mpi_accumulate(&tail_val, 1, MPI_INT, target_rank, gap_disp, MPI_REPLACE, &queue->win_cells));
            
            printf("[ENQUEUE][rank %d] Contention at pos %d. Cell rank=%d, marking gap=%d\n", 
                   mpi_get_rank(&queue->mpi_ctx), pos, cell_rank, queue->tail);
            // Continue loop (Line 12: end while)
        } else {
            // Line 8: AtomicWrite(cells[tail(mod N)].data, data)
            // Write: MPI_Accumulate with MPI_REPLACE
            MPI_Aint data_disp = pos * sizeof(spmc_cell_t) + offsetof(spmc_cell_t, data);
            MPI_TRY(mpi_accumulate(&value, 1, MPI_INT, target_rank, data_disp, MPI_REPLACE, &queue->win_cells));

            // Line 9: AtomicWrite(cells[tail(mod N)].rank, tail)
            // AtomicWrite: MPI_Accumulate with MPI_REPLACE
            int tail_val = queue->tail;
            MPI_TRY(mpi_accumulate(&tail_val, 1, MPI_INT, target_rank, rank_disp, MPI_REPLACE, &queue->win_cells));
            
            printf("[ENQUEUE][rank %d] Enqueued item: %d at pos %d | tail=%d\n",
                   mpi_get_rank(&queue->mpi_ctx), value, pos, queue->tail);
            
            // Line 10: success ← TRUE
            success = true;
        }
    }
    
    // Line 13: tail ← tail + 1
    queue->tail++;
    
    return MPI_SUCCESS;
}

/**
 * @brief Dequeues an item using FFQ logic.
 * @return Number of items dequeued (0 if queue is empty or timeout).
 */
int spmc_queue_dequeue(spmc_queue_t *queue, int *out_data, int max_count) {
    if (!out_data || max_count <= 0) return 0;
    
    int target_rank = queue->queue_owner_rank;  // Target the queue owner
    int rank;
    int retry_count = 0;
    
    // Allocate buffer for batch read
    spmc_cell_t *c = malloc(max_count * sizeof(spmc_cell_t));
    if (!c) return 0;  // Failed to allocate, return 0 items dequeued
    
    // Line 1: rank ← FetchInc(head, k)
    MPI_TRY(mpi_fetch_and_op(&max_count, &rank, MPI_INT, target_rank, 0, MPI_SUM, &queue->win_head));
    retry_count++;  // Increment retry count on first FetchInc
    
    // Line 2: while TRUE do
    while (retry_count <= MAX_DEQUEUE_RETRIES) {
        // Line 3: i ← 0, dequeued ← 0
        int i = 0;
        int dequeued = 0;
        bool timeout_occurred = false;  // Flag to track timeout
        
        // Line 4: c ← ReadCompositeSnap(cells[rank : rank + k])
        spmc_cell_t no_op_val = {0};
        int pos = rank % queue->size;
        MPI_Aint disp = pos * sizeof(spmc_cell_t);
        MPI_TRY(mpi_get_accumulate(&no_op_val, 3 * max_count, MPI_INT,
                                    c, 3 * max_count, MPI_INT,
                                    target_rank, disp, 3 * max_count, MPI_INT,
                                    MPI_NO_OP, &queue->win_cells));
        int wait_count = 0;
        
        // Line 5: while i < k do
        while(i < max_count) {
            // Line 6: if c[i].rank = rank + i then
            if (c[i].rank == rank + i) {
                // Line 7: data[i] ← c[i].data
                out_data[dequeued] = c[i].data;

                // Line 8: AtomicWrite(cells[rank + i].rank, −1)
                int cell_pos = (rank + i) % queue->size;
                MPI_Aint cell_disp = cell_pos * sizeof(spmc_cell_t);
                MPI_Aint rank_disp = cell_disp + offsetof(spmc_cell_t, rank);
                int empty_val = EMPTY_CELL;
                MPI_TRY(mpi_accumulate(&empty_val, 1, MPI_INT, target_rank, rank_disp, MPI_REPLACE, &queue->win_cells));
                
                printf("[DEQUEUE][rank %d] SUCCESS: Dequeued data=%d at pos=%d, rank=%d (retries=%d, waits=%d)\n", 
                       mpi_get_rank(&queue->mpi_ctx), out_data[dequeued], cell_pos, rank + i, retry_count, wait_count);

                // Line 9: dequeued ← dequeued + 1
                dequeued++;
                // Line 10: i ← i + 1
                i++;
                wait_count = 0;  // Reset wait count on success
            } else if(c[i].gap >= rank + i) {
                // Line 11: else if c[i].gap ≥ rank + i then
                // Line 12: i ← i + 1
                printf("[DEQUEUE][rank %d] Skipping rank %d (overtaken, gap=%d)\n",
                       mpi_get_rank(&queue->mpi_ctx), rank + i, c[i].gap);
                i++;
                wait_count = 0;  // Reset wait count
            } else {
                // Line 13: else
                // Check if we've waited too long - cell is still empty
                if (wait_count >= MAX_WAIT_COUNT) {
                    printf("[DEQUEUE][rank %d] TIMEOUT: Cell at rank %d still empty after %d waits (cell.rank=%d, cell.gap=%d)\n",
                           mpi_get_rank(&queue->mpi_ctx), rank + i, wait_count, c[i].rank, c[i].gap);
                    timeout_occurred = true;  // Set timeout flag
                    break;  // Exit inner loop
                }
                
                // Line 14: wait()
                wait_count++;
                usleep(100);  // Increased wait time for remote operations
                
                // Line 15: Re-read c ← ReadCompositeSnap(cells[rank + i : rank + k])
                spmc_cell_t no_op_val_wait = {0};
                int remaining_count = max_count - i;
                int read_pos = (rank + i) % queue->size;
                MPI_Aint read_disp = read_pos * sizeof(spmc_cell_t);
                MPI_TRY(mpi_get_accumulate(&no_op_val_wait, 3 * remaining_count, MPI_INT,
                                            &c[i], 3 * remaining_count, MPI_INT,
                                            target_rank, read_disp, 3 * remaining_count, MPI_INT,
                                            MPI_NO_OP, &queue->win_cells));
                // Continue loop to re-check c[i]
            }
        }
        
        // Line 18: if i = k ∧ dequeued > 0 then
        if(i == max_count && dequeued > 0) {
            // Line 19: return data
            printf("[DEQUEUE][rank %d] Returning %d items successfully dequeued\n", 
                   mpi_get_rank(&queue->mpi_ctx), dequeued);
            free(c);
            return dequeued;
        }
        
        // If we got some items but didn't complete the batch, return what we have
        if (dequeued > 0) {
            printf("[DEQUEUE][rank %d] Partial dequeue: returning %d items (requested %d)\n",
                   mpi_get_rank(&queue->mpi_ctx), dequeued, max_count);
            free(c);
            return dequeued;
        }
        
        // Check if timeout occurred - exit immediately without retry
        if (timeout_occurred) {
            printf("[DEQUEUE][rank %d] Queue empty (timeout after %d waits)\n",
                   mpi_get_rank(&queue->mpi_ctx), wait_count);
            break;  // Exit outer loop - don't retry
        }
        
        // Line 21: if i = k then (completed iteration but no items)
        if(i == max_count && retry_count < MAX_DEQUEUE_RETRIES) {
            // Line 22: rank ← FetchInc(head, k)
            printf("[DEQUEUE][rank %d] No items in current batch, trying next batch (retry %d/%d)\n",
                   mpi_get_rank(&queue->mpi_ctx), retry_count, MAX_DEQUEUE_RETRIES);
            MPI_TRY(mpi_fetch_and_op(&max_count, &rank, MPI_INT, target_rank, 0, MPI_SUM, &queue->win_head));
            retry_count++;  // Increment retry count on each new FetchInc
        } else {
            // Exceeded retry limit
            printf("[DEQUEUE][rank %d] Giving up after %d retries\n",
                   mpi_get_rank(&queue->mpi_ctx), retry_count);
            break;
        }
    }
    
    free(c);
    
    // Return 0 if we exceeded retry or wait limits
    return 0;
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

int spmc_queue_get_batch_size(spmc_queue_t *queue) {

    return BATCH_SIZE; // Default batch size for bdFFQ
}