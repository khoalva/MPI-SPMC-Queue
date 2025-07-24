#include "spmc_queue.h"
#include <stdio.h>
#include <stdlib.h>

int spmc_queue_init(spmc_queue_t *queue, int argc, char *argv[]) {
    if (!queue) return -1;
    
    // Initialize MPI using the custom library
    MPI_TRY(mpi_init(argc, argv, &queue->mpi_ctx));
    
    if (mpi_get_size(&queue->mpi_ctx) < 2) {
        fprintf(stderr, "At least two processes are required\n");
        mpi_finalize();
        return -1;
    }
    
    // Initialize queue metadata
    queue->row = 0;
    queue->eng_row = 0;
    queue->tail = 0;
    
    // Allocate memory using the custom library
    queue->head = mpi_calloc(MAX_ROWS * sizeof(int), 0, mpi_get_rank(&queue->mpi_ctx));
    queue->items = mpi_calloc(MAX_ROWS * MAX_COLS * sizeof(int), 0, mpi_get_rank(&queue->mpi_ctx));
    
    if (mpi_is_root(&queue->mpi_ctx)) {
        if (!queue->head || !queue->items) {
            fprintf(stderr, "Memory allocation failed\n");
            return -1;
        }
        
        // Initialize all items to L (⊥)
        for (int i = 0; i < MAX_ROWS * MAX_COLS; i++) {
            queue->items[i] = L;
        }

        // Place the end-of-row marker at the last column of each row.
        // This column is reserved and cannot be used for data.
        for (int r = 0; r < MAX_ROWS; ++r) {
            queue->items[r * MAX_COLS + (MAX_COLS - 1)] = T;
        }
        
        // Initialize head array to 0
        for (int i = 0; i < MAX_ROWS; i++) {
            queue->head[i] = 0;
        }
    }
    
    // Create MPI windows using the custom library
    MPI_TRY(mpi_win_create(queue->head, 
                           mpi_is_root(&queue->mpi_ctx) ? MAX_ROWS * sizeof(int) : 0,
                           sizeof(int), queue->mpi_ctx.comm, &queue->win_head));
    
    MPI_TRY(mpi_win_create(queue->items, 
                           mpi_is_root(&queue->mpi_ctx) ? MAX_ROWS * MAX_COLS * sizeof(int) : 0,
                           sizeof(int), queue->mpi_ctx.comm, &queue->win_items));
    
    MPI_TRY(mpi_win_create(&queue->row, 
                           mpi_is_root(&queue->mpi_ctx) ? sizeof(int) : 0,
                           sizeof(int), queue->mpi_ctx.comm, &queue->win_row));
    
    // Lock all windows for passive target synchronization
    mpi_window_t windows[] = {queue->win_head, queue->win_items, queue->win_row};
    MPI_TRY(mpi_win_lock_all_multiple(windows, 3));
    
    printf("SPMC Queue initialized successfully on rank %d/%d\n", 
           mpi_get_rank(&queue->mpi_ctx), mpi_get_size(&queue->mpi_ctx));
    
    return MPI_SUCCESS;
}

void spmc_queue_destroy(spmc_queue_t *queue) {
    if (!queue) return;
    
    mpi_window_t windows[] = {queue->win_head, queue->win_items, queue->win_row};
    mpi_win_unlock_all_multiple(windows, 3);
    
    mpi_win_destroy(&queue->win_head);
    mpi_win_destroy(&queue->win_items);
    mpi_win_destroy(&queue->win_row);
    
    mpi_free(queue->head, 0, mpi_get_rank(&queue->mpi_ctx));
    mpi_free(queue->items, 0, mpi_get_rank(&queue->mpi_ctx));
    
    mpi_finalize();
    
    printf("SPMC Queue destroyed on rank %d\n", mpi_get_rank(&queue->mpi_ctx));
}

int spmc_queue_enqueue(spmc_queue_t *queue, int value) {
    if (!spmc_queue_is_enqueuer(queue)) return -1;
    
    if (value < 0 || value > MAX_VALUE) {
        fprintf(stderr, "Invalid enqueue value: %d\n", value);
        return -1;
    }
    
    int val_from_swap;
    size_t target_offset = (queue->eng_row * MAX_COLS + queue->tail);
    
    // Step 1: Atomically swap the value into the items array.
    MPI_TRY(mpi_get_accumulate(&value, 1, MPI_INT, &val_from_swap, 1, MPI_INT, 0, target_offset, 1, MPI_INT, MPI_REPLACE, &queue->win_items));
    MPI_TRY(mpi_win_flush(0, &queue->win_items));
    
    // Step 2: Check if we hit the end-of-row marker, which we defined as T.
    if (val_from_swap == T) {
        // We hit the end of the row. We must restore the marker we just overwrote.
        int t_marker = T;
        MPI_TRY(mpi_put(&t_marker, 1, MPI_INT, 0, target_offset, &queue->win_items));
        MPI_TRY(mpi_win_flush(0, &queue->win_items));
        
        // Step 3: Increment row
        queue->eng_row++;
        if (queue->eng_row >= MAX_ROWS) {
            fprintf(stderr, "Row limit exceeded\n");
            return -1;
        }

        // Step 4: Reset tail
        queue->tail = 0;

        // Step 5: Perform the swap again at the new position (new_row, 0)
        size_t new_target_offset = (queue->eng_row * MAX_COLS + queue->tail);
        int dummy_val;
        MPI_TRY(mpi_get_accumulate(&value, 1, MPI_INT, &dummy_val, 1, MPI_INT, 0, new_target_offset, 1, MPI_INT, MPI_REPLACE, &queue->win_items));
        MPI_TRY(mpi_win_flush(0, &queue->win_items));

        // Step 6: Announce the new row to consumers.
        // The pseudo-code says Write(ROW, enq_row). This means consumers can now start dequeuing from this new row.
        int new_row_to_announce = queue->eng_row;
        MPI_TRY(mpi_put(&new_row_to_announce, 1, MPI_INT, 0, 0, &queue->win_row));
        MPI_TRY(mpi_win_flush(0, &queue->win_row));
    }
    
    // Step 7: Increment local tail
    queue->tail++;
    
    printf("Rank %d enqueued: %d (row: %d, col: %d)\n", 
           mpi_get_rank(&queue->mpi_ctx), value, queue->eng_row, queue->tail - 1);
           
    return MPI_SUCCESS;
}

int spmc_queue_dequeue(spmc_queue_t *queue) {
    if (spmc_queue_is_enqueuer(queue)) return -1;
    
    int deq_row, head_val, val;
    
    // Step 1: Read the row that is safe for dequeuing
    MPI_TRY(mpi_get(&deq_row, 1, MPI_INT, 0, 0, &queue->win_row));
    MPI_TRY(mpi_win_flush(0, &queue->win_row));
    
    // Step 2: Atomically get a unique column index for this row
    int one = 1;
    size_t head_offset = deq_row;
    MPI_TRY(mpi_fetch_and_op(&one, &head_val, MPI_INT, 0, head_offset, MPI_SUM, &queue->win_head));
    MPI_TRY(mpi_win_flush(0, &queue->win_head));

    // Check if we are trying to read the reserved end-of-row marker
    if (head_val >= MAX_COLS - 1) {
        printf("Rank %d found empty queue (row %d is full)\n", mpi_get_rank(&queue->mpi_ctx), deq_row);
        return -1;
    }
    
    // Step 3: Atomically swap the value out and put a 'T' (dequeued) marker in its place.
    int t_value = T; 
    size_t items_offset = (deq_row * MAX_COLS + head_val);
    MPI_TRY(mpi_get_accumulate(&t_value, 1, MPI_INT, &val, 1, MPI_INT, 0, items_offset, 1, MPI_INT, MPI_REPLACE, &queue->win_items));
    MPI_TRY(mpi_win_flush(0, &queue->win_items));
    
    // Step 4: Check what we got.
    if (val == L || val == T) { 
        printf("Rank %d found empty/stale cell (val=%d) at (row: %d, col: %d)\n", 
               mpi_get_rank(&queue->mpi_ctx), val, deq_row, head_val);
        return -1;
    } else {
        printf("Rank %d dequeued: %d (row: %d, col: %d)\n", 
               mpi_get_rank(&queue->mpi_ctx), val, deq_row, head_val);
        return val;
    }
}

// Unchanged functions
void spmc_queue_print_stats(spmc_queue_t *queue) {
    if (!queue) return;
    printf("SPMC Queue stats for rank %d:\n", mpi_get_rank(&queue->mpi_ctx));
    printf("   MPI size: %d\n", mpi_get_size(&queue->mpi_ctx));
    printf("   Current shared row: %d\n", queue->row);
    if (spmc_queue_is_enqueuer(queue)) {
        printf("   Enqueuer row: %d\n", queue->eng_row);
        printf("   Enqueuer tail: %d\n", queue->tail);
    }
}

int spmc_queue_is_enqueuer(spmc_queue_t *queue) {
    return queue && mpi_is_root(&queue->mpi_ctx);
}

size_t spmc_queue_get_capacity_bytes(const spmc_queue_t *queue) {
    if (!queue) return 0;
    size_t meta_bytes = 3 * sizeof(int);
    size_t head_bytes = MAX_ROWS * sizeof(int);
    size_t items_bytes = MAX_ROWS * MAX_COLS * sizeof(int);
    return meta_bytes + head_bytes + items_bytes;
}
