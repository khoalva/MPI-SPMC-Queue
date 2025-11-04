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
            MPI_TRY(mpi_accumulate(empty_vals, range_len, MPI_INT, 0, rank_disp, MPI_REPLACE, &queue->win_ranks));
            free(empty_vals);
        } else {
            /* allocation failed: fallback to per-element updates */
            for (int j = 0; j < range_len; j++) {
                int empty_val = EMPTY_CELL;
                int cell_pos = (rank + range_start + j) % queue->size;
                MPI_Aint rank_disp = cell_pos * sizeof(int);
                MPI_TRY(mpi_accumulate(&empty_val, 1, MPI_INT, 0, rank_disp, MPI_REPLACE, &queue->win_ranks));
            }
        }
    }
}
 

/**
 * @brief Initializes the SPMC queue and necessary MPI resources.
 */
int spmc_queue_init(spmc_queue_t *queue, int argc, char *argv[]) {
    if (!queue) return -1;

    MPI_TRY(mpi_init(argc, argv, &queue->mpi_ctx));

    if (mpi_get_size(&queue->mpi_ctx) < 2) {
        // fprintf(stderr, "At least two processes (1 producer, 1+ consumer) are required.\n");
        mpi_finalize();
        return -1;
    }

    queue->size = MAX_QUEUE_SIZE;
    int is_producer = (mpi_get_rank(&queue->mpi_ctx) == 0);

    // Only the producer (rank 0) allocates and initializes the queue memory.
    if (is_producer) {
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
        // Consumers do not allocate the main memory.
        queue->ranks = NULL;
        queue->gaps = NULL;
        queue->datas = NULL;
        queue->head = 0;
        queue->tail = 0;
    }

    // Create MPI windows for one-sided access. The size is 0 for consumers.
    size_t array_size = is_producer ? queue->size * sizeof(int) : 0;
    size_t head_size = is_producer ? sizeof(int) : 0;
    MPI_TRY(mpi_win_create(queue->ranks, array_size, sizeof(int), queue->mpi_ctx.comm, &queue->win_ranks));
    MPI_TRY(mpi_win_create(queue->gaps, array_size, sizeof(int), queue->mpi_ctx.comm, &queue->win_gaps));
    MPI_TRY(mpi_win_create(queue->datas, array_size, sizeof(int), queue->mpi_ctx.comm, &queue->win_datas));
    MPI_TRY(mpi_win_create(&queue->head, head_size, sizeof(int), queue->mpi_ctx.comm, &queue->win_head));

    // Lock all windows to enable passive target synchronization, which allows
    // consumers to perform RMA operations without explicit calls from the producer.
    mpi_window_t windows[] = {queue->win_ranks, queue->win_gaps, queue->win_datas, queue->win_head};
    MPI_TRY(mpi_win_lock_all_multiple(windows, 4));

    printf("SPMC Queue initialized on rank %d/%d\n",
           mpi_get_rank(&queue->mpi_ctx), mpi_get_size(&queue->mpi_ctx));

    return MPI_SUCCESS;
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
    
    bool success = false;
    
    // Line 3: while ¬success do
    while (!success) {
        // Line 4: rank = AtomicRead(cells[tail(mod N)].rank)
        int pos = queue->tail % queue->size;
        MPI_Aint rank_disp = pos * sizeof(int);
        
        // AtomicRead: MPI_Fetch_and_op with MPI_NO_OP
        int cell_rank;
        int no_op_val = 0;
        MPI_TRY(mpi_fetch_and_op(&no_op_val, &cell_rank, MPI_INT, 0, rank_disp, MPI_NO_OP, &queue->win_ranks));
        
        // Line 5: if rank ≥ 0 then
        if (cell_rank >= 0) {
            // Line 6: AtomicWrite(cells[tail(mod N)].gap, tail)
            // AtomicWrite: MPI_Accumulate with MPI_REPLACE
            MPI_Aint gap_disp = pos * sizeof(int);
            int tail_val = queue->tail;
            MPI_TRY(mpi_accumulate(&tail_val, 1, MPI_INT, 0, gap_disp, MPI_REPLACE, &queue->win_gaps));
            
            // printf("[ENQUEUE][rank %d] Contention at pos %d. Cell rank=%d, marking gap=%d\n", 
            //        mpi_get_rank(&queue->mpi_ctx), pos, cell_rank, queue->tail);
            // Continue loop (Line 12: end while)
        } else {
            // Line 8: Write(cells[tail(mod N)].data, data)
            // Write: MPI_Put (non-atomic)
            MPI_Aint data_disp = pos * sizeof(int);
            MPI_TRY(mpi_put(&value, 1, MPI_INT, 0, data_disp, &queue->win_datas));
            
            // Line 9: AtomicWrite(cells[tail(mod N)].rank, tail)
            // AtomicWrite: MPI_Accumulate with MPI_REPLACE
            int tail_val = queue->tail;
            MPI_TRY(mpi_accumulate(&tail_val, 1, MPI_INT, 0, rank_disp, MPI_REPLACE, &queue->win_ranks));
            
            // printf("[ENQUEUE][rank %d] Enqueued item: %d at pos %d | tail=%d\n",
            //        mpi_get_rank(&queue->mpi_ctx), value, pos, queue->tail);
            
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
 * @return Number of items dequeued.
 */
int spmc_queue_dequeue(spmc_queue_t *queue, int *out_data, int max_count) {
    if (!out_data || max_count <= 0) return 0;
    
    int rank;
    int retry_count = 0;
    int wait_count = 0;
    
    // Line 1: rank ← FetchInc(head, k)
    MPI_TRY(mpi_fetch_and_op(&max_count, &rank, MPI_INT, 0, 0, MPI_SUM, &queue->win_head));
    
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
    if (!pending || !skipped) {
        if (pending) free(pending);
        if (skipped) free(skipped);
        free(ranks_buf);
        free(gaps_buf);
        free(datas_buf);
        return 0;
    }
    
    for (int i = 0; i < max_count; i++) {
        pending[i] = true;
        skipped[i] = false;
    }
    
    // Line 3: while TRUE do
    while (retry_count < MAX_DEQUEUE_RETRIES && wait_count < MAX_WAIT_COUNT) {
        retry_count++;
        
        // Line 4: rankSnap ← ReadCompositeSnap(ranks[rank : rank + k])
        int no_op_val = 0;
        int pos = rank % queue->size;
        MPI_Aint disp = pos * sizeof(int);
        MPI_TRY(mpi_get_accumulate(&no_op_val, max_count, MPI_INT,
                                    ranks_buf, max_count, MPI_INT,
                                    0, disp, max_count, MPI_INT,
                                    MPI_NO_OP, &queue->win_ranks));
        
        // Line 5: pending ← {i ∈ pending | rankSnap[i] ≠ rank + i}
        for (int i = 0; i < max_count; i++) {
            if (pending[i] && ranks_buf[i] == rank + i) {
                pending[i] = false;
            }
        }
        
        // Line 6: if pending = [] then
        bool all_done = true;
        for (int i = 0; i < max_count; i++) {
            if (pending[i]) {
                all_done = false;
                break;
            }
        }
        
        if (all_done) {
            // Line 7: data ← Read(datas[rank : rank + k])
            MPI_Aint data_disp = pos * sizeof(int);
            MPI_TRY(mpi_get(datas_buf, max_count, MPI_INT, 0, data_disp, &queue->win_datas));
            
            for (int i = 0; i < max_count; i++) {
                out_data[i] = datas_buf[i];
            }
            
            // Line 8: AtomicWrite(ranks[rank + i : rank + k], -1) for i ∉ skipped
            // Implemented by helper to update continuous non-skipped ranges
            update_ranks_ranges(queue, rank, skipped, max_count);
            
            // Line 9: return data
            free(pending);
            free(skipped);
            free(ranks_buf);
            free(gaps_buf);
            free(datas_buf);
            return max_count;
        }
        
        // Line 11: gapSnap ← ReadCompositeSnap(gaps[rank : rank + k])
        MPI_TRY(mpi_get_accumulate(&no_op_val, max_count, MPI_INT,
                                    gaps_buf, max_count, MPI_INT,
                                    0, disp, max_count, MPI_INT,
                                    MPI_NO_OP, &queue->win_gaps));
        
        // Line 12: skipped ← skipped ∪ {i ∈ pending | gapSnap[i] ≥ rank + i}
        // Line 13: pending ← {i ∈ pending | gapSnap[i] < rank + i}
        for (int i = 0; i < max_count; i++) {
            if (pending[i] && gaps_buf[i] >= rank + i) {
                skipped[i] = true;
                pending[i] = false;
            }
        }
        
        // Line 14: if pending ≠ [] then
        bool has_pending = false;
        for (int i = 0; i < max_count; i++) {
            if (pending[i]) {
                has_pending = true;
                break;
            }
        }
        
        if (has_pending) {
            // Line 15: wait()
            wait_count++;
            usleep(10);
        }
        // Line 17: end while
    }
    
    // Timeout or retry limit reached
    free(pending);
    free(skipped);
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