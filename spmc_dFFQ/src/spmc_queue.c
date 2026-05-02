#define _POSIX_C_SOURCE 199309L
#define _GNU_SOURCE  // Add this for broader usleep support
#define MAX_DEQUEUE_RETRIES 10
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
        // fprintf(stderr, "At least two processes (1 producer, 1+ consumer) are required.\n");
        mpi_finalize();
        return -1;
    }

    queue->size = MAX_QUEUE_SIZE;
    int is_producer = (mpi_get_rank(&queue->mpi_ctx) == 0);

    // Only the producer (rank 0) allocates and initializes the queue memory.
    if (is_producer) {
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
        MPI_Aint rank_disp = pos * sizeof(spmc_cell_t) + offsetof(spmc_cell_t, rank);
        
        // AtomicRead: MPI_Fetch_and_op with MPI_NO_OP
        int cell_rank;
        int no_op_val = 0;
        MPI_TRY(mpi_fetch_and_op(&no_op_val, &cell_rank, MPI_INT, 0, rank_disp, MPI_NO_OP, &queue->win_cells));
        
        // Line 5: if rank ≥ 0 then
        if (cell_rank >= 0) {
            // Line 6: AtomicWrite(cells[tail(mod N)].gap, tail)
            // AtomicWrite: MPI_Accumulate with MPI_REPLACE
            MPI_Aint gap_disp = pos * sizeof(spmc_cell_t) + offsetof(spmc_cell_t, gap);
            int tail_val = queue->tail;
            MPI_TRY(mpi_accumulate(&tail_val, 1, MPI_INT, 0, gap_disp, MPI_REPLACE, &queue->win_cells));
            
            // printf("[ENQUEUE][rank %d] Contention at pos %d. Cell rank=%d, marking gap=%d\n", 
            //        mpi_get_rank(&queue->mpi_ctx), pos, cell_rank, queue->tail);
            // Continue loop (Line 12: end while)
        } else {
            // Line 8: Write(cells[tail(mod N)].data, data)
            // Write: MPI_Put (non-atomic)
            MPI_Aint data_disp = pos * sizeof(spmc_cell_t) + offsetof(spmc_cell_t, data);
            MPI_TRY(mpi_put(&value, sizeof(int), MPI_BYTE, 0, data_disp, &queue->win_cells));
            
            // Line 9: AtomicWrite(cells[tail(mod N)].rank, tail)
            // AtomicWrite: MPI_Accumulate with MPI_REPLACE
            int tail_val = queue->tail;
            MPI_TRY(mpi_accumulate(&tail_val, 1, MPI_INT, 0, rank_disp, MPI_REPLACE, &queue->win_cells));
            
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
 * @return MPI_SUCCESS on success, -1 on failure.
 */
int spmc_queue_dequeue(spmc_queue_t *queue, int *out_data, int max_count) {
    if (!out_data || max_count <= 0) return 0;
    
    // dFFQ only supports single dequeue
    int rank;
    int one = 1;
    bool success = false;
    int retry_count = 0;
    int wait_count = 0;
    int data = -1;
    int no_op_val = 0;
    
    // Line 1: rank ← FetchInc(head)
    MPI_TRY(mpi_fetch_and_op(&one, &rank, MPI_INT, 0, 0, MPI_SUM, &queue->win_head));
    
    retry_count++;
    int pos = rank % queue->size;
    MPI_Aint disp = pos * sizeof(spmc_cell_t);
    
    // Line 2: success ← FALSE
    // Line 3: while ¬success do
    while (!success && retry_count < MAX_DEQUEUE_RETRIES && wait_count < 10) {
        // Line 4: cellRank ← AtomicRead(cells[rank(mod N)].rank)
        int cellRank;
        MPI_Aint rank_disp = disp + offsetof(spmc_cell_t, rank);
        MPI_TRY(mpi_fetch_and_op(&no_op_val, &cellRank, MPI_INT, 0, rank_disp, MPI_NO_OP, &queue->win_cells));
        
        // Line 5: if cellRank = rank then
        if (cellRank == rank) {
            // Line 6: data ← Read(cells[rank(mod N)].data)
            MPI_Aint data_disp = disp + offsetof(spmc_cell_t, data);
            MPI_TRY(mpi_get(&data, sizeof(int), MPI_BYTE, 0, data_disp, &queue->win_cells));
            
            // Line 7: AtomicWrite(cells[rank(mod N)].rank, −1)
            int empty_val = EMPTY_CELL;
            MPI_TRY(mpi_accumulate(&empty_val, 1, MPI_INT, 0, rank_disp, MPI_REPLACE, &queue->win_cells));
            
            // printf("[DEQUEUE][rank %d] SUCCESS: Dequeued data=%d at pos=%d, rank=%d (waited %d times)\n", 
            //        mpi_get_rank(&queue->mpi_ctx), data, pos, rank, wait_count);
            
            // Line 8: success ← TRUE
            success = true;
        } else {
            // Line 10: gap ← AtomicRead(cells[rank(mod N)].gap)
            int gap;
            MPI_Aint gap_disp = disp + offsetof(spmc_cell_t, gap);
            MPI_TRY(mpi_fetch_and_op(&no_op_val, &gap, MPI_INT, 0, gap_disp, MPI_NO_OP, &queue->win_cells));
            
            // Line 11: if gap ≥ rank then
            if (gap >= rank) {
                // Line 12: cellRank ← AtomicRead(cells[rank(mod N)].rank)
                MPI_TRY(mpi_fetch_and_op(&no_op_val, &cellRank, MPI_INT, 0, rank_disp, MPI_NO_OP, &queue->win_cells));
                
                // Line 13: if cellRank ≠ rank then
                if (cellRank != rank) {
                    // Line 14: rank ← FetchInc(head)
                    MPI_TRY(mpi_fetch_and_op(&one, &rank, MPI_INT, 0, 0, MPI_SUM, &queue->win_head));
                    pos = rank % queue->size;
                    disp = pos * sizeof(spmc_cell_t);
                    retry_count++;
                    wait_count = 0;
                    
                    // printf("[DEQUEUE][rank %d] Gap detected! Fetching new rank=%d, pos=%d (retry #%d)\n", 
                    //        mpi_get_rank(&queue->mpi_ctx), rank, pos, retry_count);
                } else {
                    // Line 16: wait()
                    wait_count++;
                    // usleep(10);
                }
            } else {
                // Line 19: wait()
                wait_count++;
                // usleep(10);
            }
        }
    }
    
    // Line 24: return data
    if (data == -1) {
        return 0;  // No items dequeued
    } else {
        out_data[0] = data;  // Store the single dequeued value
        return 1;  // Successfully dequeued 1 item
    }
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
 * @brief Separate batch-size queries for producer and consumer sides.
 */
int spmc_queue_get_enq_batch_size(spmc_queue_t *queue) {
    (void)queue;
    return 1;  /* dFFQ enqueues one item at a time */
}

int spmc_queue_get_deq_batch_size(spmc_queue_t *queue) {
    (void)queue;
    return 1;  /* dFFQ dequeues one item at a time */
}

/* Legacy alias */
int spmc_queue_get_batch_size(spmc_queue_t *queue) {
    return spmc_queue_get_enq_batch_size(queue);
}

/**
 * Batch enqueue: enqueues 'count' items from the 'values' array.
 * For queues without native batch support, this is a loop over single enqueue.
 * Mirrors the batch dequeue pattern: spmc_queue_dequeue(queue, buffer, max_count).
 */
int spmc_queue_enqueue_batch(spmc_queue_t *queue, int *values, int count) {
    if (!values || count <= 0) return -1;
    for (int i = 0; i < count; i++) {
        if (spmc_queue_enqueue(queue, values[i]) != MPI_SUCCESS) {
            return -1;
        }
    }
    return MPI_SUCCESS;
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