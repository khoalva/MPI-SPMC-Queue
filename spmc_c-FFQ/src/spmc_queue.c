#define _POSIX_C_SOURCE 199309L
#define _GNU_SOURCE  // Add this for broader usleep support

#include "spmc_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/**
 * Helper: update continuous ranges of ranks to EMPTY_CELL using as few
 * MPI_Accumulate calls as possible. Scans the boolean "skipped" array to
 * find continuous ranges of non-skipped indices and updates each range with
 * a single MPI_Accumulate. Falls back to per-element updates if allocation
 * of the temporary buffer fails.
 */
static void update_ranks_ranges(spmc_queue_t *queue, int rank, bool *skipped, int max_count) {
    int target_rank = queue->queue_owner_rank;  // Target the queue owner
    int i = 0;
    while (i < max_count) {
        /* skip skipped elements */
        while (i < max_count && skipped[i]) i++;
        if (i >= max_count) break;

        int range_start = i;
        int range_len = 0;
        while (i < max_count && !skipped[i]) {
            range_len++;
            i++;
        }

        if (range_len <= 0) continue;

        /* try to update the whole continuous range in one call */
        int *empty_vals = malloc(range_len * sizeof(int));
        if (empty_vals) {
            for (int j = 0; j < range_len; j++) empty_vals[j] = EMPTY_CELL;
            int cell_pos = (rank + range_start) % queue->size;
            MPI_Aint rank_disp = cell_pos * sizeof(int);
            MPI_TRY(mpi_accumulate(empty_vals, range_len, MPI_INT, target_rank, rank_disp, MPI_REPLACE, &queue->win_ranks));
            free(empty_vals);
        } else {
            /* allocation failed: fallback to per-element updates */
            for (int j = 0; j < range_len; j++) {
                int empty_val = EMPTY_CELL;
                int cell_pos = (rank + range_start + j) % queue->size;
                MPI_Aint rank_disp = cell_pos * sizeof(int);
                MPI_TRY(mpi_accumulate(&empty_val, 1, MPI_INT, target_rank, rank_disp, MPI_REPLACE, &queue->win_ranks));
            }
        }
    }
}
 

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
        queue->ranks = malloc(queue->size * sizeof(int));
        queue->gaps = malloc(queue->size * sizeof(int));
        queue->datas = malloc(queue->size * sizeof(int));
        if (!queue->ranks || !queue->gaps || !queue->datas) {
            // fprintf(stderr, "Failed to allocate memory for queue arrays.\n");
            if (queue->ranks) free(queue->ranks);
            if (queue->gaps) free(queue->gaps);
            if (queue->datas) free(queue->datas);
            mpi_finalize();
            return -1;
        }
        queue->head = 0;
        queue->tail = 0;
        for (int i = 0; i < queue->size; i++) {
            queue->ranks[i] = EMPTY_CELL;
            queue->gaps[i] = 0;
            queue->datas[i] = 0;
        }
    } else {
        // Non-queue-owner nodes do not allocate the main memory.
        queue->ranks = NULL;
        queue->gaps = NULL;
        queue->datas = NULL;
        queue->head = 0;
        queue->tail = 0;
    }

    // Create MPI windows for one-sided access. The size is 0 for non-queue-owner.
    size_t array_size = is_queue_owner ? queue->size * sizeof(int) : 0;
    size_t head_size = is_queue_owner ? sizeof(int) : 0;
    MPI_TRY(mpi_win_create(queue->ranks, array_size, sizeof(int), queue->mpi_ctx.comm, &queue->win_ranks));
    MPI_TRY(mpi_win_create(queue->gaps, array_size, sizeof(int), queue->mpi_ctx.comm, &queue->win_gaps));
    MPI_TRY(mpi_win_create(queue->datas, array_size, sizeof(int), queue->mpi_ctx.comm, &queue->win_datas));
    MPI_TRY(mpi_win_create(&queue->head, head_size, sizeof(int), queue->mpi_ctx.comm, &queue->win_head));

    // Lock all windows to enable passive target synchronization, which allows
    // consumers to perform RMA operations without explicit calls from the producer.
    mpi_window_t windows[] = {queue->win_ranks, queue->win_gaps, queue->win_datas, queue->win_head};
    MPI_TRY(mpi_win_lock_all_multiple(windows, 4));

    printf("r-FFQ SPMC Queue initialized on rank %d/%d (queue_owner=%d)\n",
           rank, size, queue_owner_rank);

    return MPI_SUCCESS;
}

// Backward compatibility: default queue owner at rank 0
int spmc_queue_init(spmc_queue_t *queue, int argc, char *argv[]) {
    return spmc_queue_init_with_queue_owner(queue, argc, argv, 1);
}

/**
 * @brief Destroys the queue and frees all associated resources.
 */
void spmc_queue_destroy(spmc_queue_t *queue) {
    if (!queue) return;

    mpi_window_t windows[] = {queue->win_ranks, queue->win_gaps, queue->win_datas, queue->win_head};
    mpi_win_unlock_all_multiple(windows, 4);

    mpi_win_destroy(&queue->win_ranks);
    mpi_win_destroy(&queue->win_gaps);
    mpi_win_destroy(&queue->win_datas);
    mpi_win_destroy(&queue->win_head);

    // The producer frees the memory it allocated.
    if (spmc_queue_is_enqueuer(queue)) {
        if (queue->ranks) free(queue->ranks);
        if (queue->gaps) free(queue->gaps);
        if (queue->datas) free(queue->datas);
        queue->ranks = NULL;
        queue->gaps = NULL;
        queue->datas = NULL;
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
    
    printf("[ENQUEUE] Rank %d: Starting enqueue value=%d, target_rank=%d, tail=%d\n", 
           mpi_get_rank(&queue->mpi_ctx), value, target_rank, queue->tail);
    
    // Line 3: while ¬success do
    while (!success) {
        // Line 4: rank = AtomicRead(cells[tail(mod N)].rank)
        int pos = queue->tail % queue->size;
        MPI_Aint rank_disp = pos * sizeof(int);
        
        // AtomicRead: MPI_Fetch_and_op with MPI_NO_OP
        int cell_rank;
        int no_op_val = 0;
        MPI_TRY(mpi_fetch_and_op(&no_op_val, &cell_rank, MPI_INT, target_rank, rank_disp, MPI_NO_OP, &queue->win_ranks));
        
        printf("[ENQUEUE] Rank %d: pos=%d, cell_rank=%d\n", 
               mpi_get_rank(&queue->mpi_ctx), pos, cell_rank);
        
        // Line 5: if rank ≥ 0 then
        if (cell_rank >= 0) {
            // Line 6: AtomicWrite(cells[tail(mod N)].gap, tail)
            // AtomicWrite: MPI_Accumulate with MPI_REPLACE
            MPI_Aint gap_disp = pos * sizeof(int);
            int tail_val = queue->tail;
            MPI_TRY(mpi_accumulate(&tail_val, 1, MPI_INT, target_rank, gap_disp, MPI_REPLACE, &queue->win_gaps));
            
            printf("[ENQUEUE] Rank %d: Contention at pos %d. Cell rank=%d, marking gap=%d\n", 
                   mpi_get_rank(&queue->mpi_ctx), pos, cell_rank, queue->tail);
            // Continue loop (Line 12: end while)
        } else {
            // Line 8: Write(cells[tail(mod N)].data, data)
            // Write: MPI_Put (non-atomic)
            MPI_Aint data_disp = pos * sizeof(int);
            MPI_TRY(mpi_put(&value, 1, MPI_INT, target_rank, data_disp, &queue->win_datas));
            
            // Line 9: AtomicWrite(cells[tail(mod N)].rank, tail)
            // AtomicWrite: MPI_Accumulate with MPI_REPLACE
            int tail_val = queue->tail;
            MPI_TRY(mpi_accumulate(&tail_val, 1, MPI_INT, target_rank, rank_disp, MPI_REPLACE, &queue->win_ranks));
            
            printf("[ENQUEUE] Rank %d: Enqueued item: %d at pos %d | tail=%d\n",
                   mpi_get_rank(&queue->mpi_ctx), value, pos, queue->tail);
            
            // Line 10: success ← TRUE
            success = true;
        }
    }
    
    // Line 13: tail ← tail + 1
    queue->tail++;
    
    printf("[ENQUEUE] Rank %d: Completed enqueue, new tail=%d\n", 
           mpi_get_rank(&queue->mpi_ctx), queue->tail);
    
    return MPI_SUCCESS;
}

/**
 * @brief Dequeues an item using FFQ logic (refactored version).
 * @return Number of items dequeued.
 */
int spmc_queue_dequeue(spmc_queue_t *queue, int *out_data, int max_count) {
    if (!out_data || max_count <= 0) return 0;
    
    int target_rank = queue->queue_owner_rank;  // Target the queue owner
    int rank;
    int retry_count = 0;
    int wait_count = 0;
    
    printf("[DEQUEUE] Rank %d: Starting dequeue, target_rank=%d, max_count=%d\n",
           mpi_get_rank(&queue->mpi_ctx), target_rank, max_count);
    
    // Line 1: rank ← FetchInc(head, k)
    MPI_TRY(mpi_fetch_and_op(&max_count, &rank, MPI_INT, target_rank, 0, MPI_SUM, &queue->win_head));
    
    printf("[DEQUEUE] Rank %d: Got rank=%d from FetchInc(head, %d)\n",
           mpi_get_rank(&queue->mpi_ctx), rank, max_count);
    
    // Allocate buffers for batch read
    int *ranks_buf = malloc(max_count * sizeof(int));
    int *gaps_buf = malloc(max_count * sizeof(int));
    int *datas_buf = malloc(max_count * sizeof(int));
    if (!ranks_buf || !gaps_buf || !datas_buf) {
        if (ranks_buf) free(ranks_buf);
        if (gaps_buf) free(gaps_buf);
        if (datas_buf) free(datas_buf);
        return 0;  // Failed to allocate, return 0 items dequeued
    }
    
    // Line 2: pending ← [0, 1, ..., k-1], skipped ← []
    bool *pending = malloc(max_count * sizeof(bool));
    bool *skipped = malloc(max_count * sizeof(bool));
    bool *ready = malloc(max_count * sizeof(bool));
    if (!pending || !skipped || !ready) {
        if (pending) free(pending);
        if (skipped) free(skipped);
        if (ready) free(ready);
        free(ranks_buf);
        free(gaps_buf);
        free(datas_buf);
        return 0;
    }
    
    for (int i = 0; i < max_count; i++) {
        pending[i] = true;
        skipped[i] = false;
        ready[i] = false;
    }
    
    int no_op_val = 0;
    int pos = rank % queue->size;
    MPI_Aint disp = pos * sizeof(int);
    
    printf("[DEQUEUE] Rank %d: pos=%d, disp=%ld\n",
           mpi_get_rank(&queue->mpi_ctx), pos, disp);
    
    // Line 3: while TRUE do
    while (retry_count < MAX_DEQUEUE_RETRIES && wait_count < MAX_WAIT_COUNT) {
        retry_count++;
        
        printf("[DEQUEUE] Rank %d: Retry %d - Reading ranks snapshot\n",
               mpi_get_rank(&queue->mpi_ctx), retry_count);
        
        // Line 4: rankSnap ← ReadCompositeSnap(ranks[rank : rank + k])
        MPI_TRY(mpi_get_accumulate(&no_op_val, max_count, MPI_INT,
                                    ranks_buf, max_count, MPI_INT,
                                    target_rank, disp, max_count, MPI_INT,
                                    MPI_NO_OP, &queue->win_ranks));
        
        printf("[DEQUEUE] Rank %d: ranks_buf = [", mpi_get_rank(&queue->mpi_ctx));
        for (int i = 0; i < max_count; i++) {
            printf("%d%s", ranks_buf[i], i < max_count - 1 ? ", " : "");
        }
        printf("]\n");
        
        // Line 5: pending ← {i ∈ pending | rankSnap[i] ≠ rank + i}
        for (int i = 0; i < max_count; i++) {
            if (pending[i] && ranks_buf[i] == rank + i) {
                pending[i] = false;
                printf("[DEQUEUE] Rank %d: Item %d matched (ranks_buf[%d]=%d == rank+i=%d), removing from pending\n",
                       mpi_get_rank(&queue->mpi_ctx), i, i, ranks_buf[i], rank + i);
            }
        }
        
        // Line 6: if pending ≠ [] then
        bool has_pending = false;
        for (int i = 0; i < max_count; i++) {
            if (pending[i]) {
                has_pending = true;
                break;
            }
        }
        
        printf("[DEQUEUE] Rank %d: has_pending=%d\n",
               mpi_get_rank(&queue->mpi_ctx), has_pending);
        
        if (has_pending) {
            // Line 7: gapSnap ← ReadCompositeSnap(gaps[rank : rank + k])
            MPI_TRY(mpi_get_accumulate(&no_op_val, max_count, MPI_INT,
                                        gaps_buf, max_count, MPI_INT,
                                        target_rank, disp, max_count, MPI_INT,
                                        MPI_NO_OP, &queue->win_gaps));
            
            // Line 8: ready ← {i ∈ pending | gapSnap[i] ≥ rank + i}
            bool has_ready = false;
            for (int i = 0; i < max_count; i++) {
                ready[i] = false;
                if (pending[i] && gaps_buf[i] >= rank + i) {
                    ready[i] = true;
                    has_ready = true;
                }
            }
            
            // Line 9: if ready ≠ [] then
            if (has_ready) {
                // Line 10: rankSnap ← ReadCompositeSnap(ranks[rank : rank + k])
                MPI_TRY(mpi_get_accumulate(&no_op_val, max_count, MPI_INT,
                                            ranks_buf, max_count, MPI_INT,
                                            target_rank, disp, max_count, MPI_INT,
                                            MPI_NO_OP, &queue->win_ranks));
                
                // Line 11: skipped ← skipped ∪ {i ∈ ready | rankSnap[i] ≠ rank + i}
                for (int i = 0; i < max_count; i++) {
                    if (ready[i] && ranks_buf[i] != rank + i) {
                        skipped[i] = true;
                    }
                }
                
                // Line 12: pending ← pending \ ready
                for (int i = 0; i < max_count; i++) {
                    if (ready[i]) {
                        pending[i] = false;
                    }
                }
            }
            // Line 13: end if
            
            // Line 14: if pending ≠ [] then
            has_pending = false;
            for (int i = 0; i < max_count; i++) {
                if (pending[i]) {
                    has_pending = true;
                    break;
                }
            }
            
            if (has_pending) {
                // Line 15: pending ← {i ∈ pending | gapSnap[i] < rank + i}
                for (int i = 0; i < max_count; i++) {
                    if (pending[i] && gaps_buf[i] >= rank + i) {
                        pending[i] = false;
                    }
                }
                
                // Line 16: wait()
                wait_count++;
                usleep(10);
                // Line 17: continue
                continue;
            }
            // Line 18: end if
        }
        // Line 19: end if
        
        // Line 20: data ← Read(datas[rank : rank + k])
        MPI_Aint data_disp = pos * sizeof(int);
        MPI_TRY(mpi_get(datas_buf, max_count, MPI_INT, target_rank, data_disp, &queue->win_datas));
        
        printf("[DEQUEUE] Rank %d: Read datas_buf = [", mpi_get_rank(&queue->mpi_ctx));
        for (int i = 0; i < max_count; i++) {
            printf("%d%s", datas_buf[i], i < max_count - 1 ? ", " : "");
        }
        printf("]\n");
        
        // Copy only non-skipped data to output (for i ∉ skipped)
        int out_idx = 0;
        for (int i = 0; i < max_count; i++) {
            if (!skipped[i]) {
                out_data[out_idx++] = datas_buf[i];
                printf("[DEQUEUE] Rank %d: Dequeued item %d: value=%d\n",
                       mpi_get_rank(&queue->mpi_ctx), i, datas_buf[i]);
            } else {
                printf("[DEQUEUE] Rank %d: Skipped item %d\n",
                       mpi_get_rank(&queue->mpi_ctx), i);
            }
        }
        
        // Line 21: AtomicWrite(ranks[rank + i : rank + k], -1) for i ∉ skipped
        update_ranks_ranges(queue, rank, skipped, max_count);
        
        // Line 22: return data (count of non-skipped items)
        int dequeued_count = 0;
        for (int i = 0; i < max_count; i++) {
            if (!skipped[i]) {
                dequeued_count++;
            }
        }
        
        printf("[DEQUEUE] Rank %d: Returning %d items (skipped=%d)\n",
               mpi_get_rank(&queue->mpi_ctx), dequeued_count, max_count - dequeued_count);
        
        free(pending);
        free(skipped);
        free(ready);
        free(ranks_buf);
        free(gaps_buf);
        free(datas_buf);
        return dequeued_count;
        
        // Line 23: end while
    }
    
    // Timeout or retry limit reached
    printf("[DEQUEUE] Rank %d: TIMEOUT - retry_count=%d, wait_count=%d\n",
           mpi_get_rank(&queue->mpi_ctx), retry_count, wait_count);
    
    free(pending);
    free(skipped);
    free(ready);
    free(ranks_buf);
    free(gaps_buf);
    free(datas_buf);
    
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
        return 3 * queue->size * sizeof(int) + sizeof(queue->head) + sizeof(queue->tail);
    }
    return 0;
}

int spmc_queue_get_batch_size(spmc_queue_t *queue) {

    return BATCH_SIZE; // Default batch size for bdFFQ
}