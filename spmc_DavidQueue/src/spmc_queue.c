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
    
    // Initialize queue metadata - CRITICAL FIX
    queue->row = 0;  // No rows completed yet
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
        
        // Initialize all items to L (⊥) - EXPLICIT initialization
        for (int i = 0; i < MAX_ROWS * MAX_COLS; i++) {
            queue->items[i] = L;
        }

        // Place the end-of-row marker (⊤) at the last column of each row
        for (int r = 0; r < MAX_ROWS; ++r) {
            queue->items[r * MAX_COLS + (MAX_COLS - 1)] = T;
        }
        
        // Initialize head array to 0
        for (int i = 0; i < MAX_ROWS; i++) {
            queue->head[i] = 0;
        }
        
        // CRITICAL: Add memory barrier after initialization
        printf("[INIT] Rank %d: Memory initialized, first few items: %d, %d, %d\n",
               mpi_get_rank(&queue->mpi_ctx), queue->items[0], queue->items[1], queue->items[2]);
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
    
    // CRITICAL: Add a barrier to ensure all processes see the initialization
    MPI_TRY(mpi_barrier(queue->mpi_ctx.comm));
    
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
    
    int val;
    size_t element_offset = (queue->eng_row * MAX_COLS + queue->tail);
    size_t byte_offset = element_offset * sizeof(int);

    // Bounds checking - CRITICAL SAFETY CHECK
    if (element_offset >= MAX_ROWS * MAX_COLS) {
        fprintf(stderr, "FATAL: Calculated element offset %zu exceeds bounds\n", element_offset);
        return -1;
    }

    // printf("[ENQ_OFFSET_DEBUG] Rank %d: Element offset = %zu, Byte offset = %zu for (row:%d, col:%d)\n",
    //        mpi_get_rank(&queue->mpi_ctx), element_offset, byte_offset, queue->eng_row, queue->tail);

    // AtomicSwap: MPI_Fetch_and_op with MPI_REPLACE
    MPI_TRY(mpi_fetch_and_op(&value, &val, MPI_INT, 0, byte_offset, MPI_REPLACE, &queue->win_items));
    
    // printf("[ENQ_SWAP_RESULT] Rank %d: Swap returned original value: %d\n",
    //        mpi_get_rank(&queue->mpi_ctx), val);
    
    // Check if we hit the end-of-row marker (T)
    if (val == T) {
        // Move to next row
        queue->eng_row++;
        if (queue->eng_row >= MAX_ROWS) {
            fprintf(stderr, "Row limit exceeded\n");
            return -1;
        }
        queue->tail = 0;

        // AtomicSwap at the new position
        size_t new_element_offset = (queue->eng_row * MAX_COLS + queue->tail);
        size_t new_byte_offset = new_element_offset * sizeof(int);
        
        // Bounds checking for new position
        if (new_element_offset >= MAX_ROWS * MAX_COLS) {
            fprintf(stderr, "FATAL: New element offset %zu exceeds bounds\n", new_element_offset);
            return -1;
        }
        
        printf("[ENQ_OFFSET_DEBUG] Rank %d: New element offset = %zu, byte offset = %zu for (row:%d, col:%d)\n",
               mpi_get_rank(&queue->mpi_ctx), new_element_offset, new_byte_offset, queue->eng_row, queue->tail);

        int dummy_val;
        MPI_TRY(mpi_fetch_and_op(&value, &dummy_val, MPI_INT, 0, new_byte_offset, MPI_REPLACE, &queue->win_items));


        // AtomicWrite ROW: MPI_Accumulate with MPI_REPLACE
        MPI_TRY(mpi_accumulate(&queue->eng_row, 1, MPI_INT, 0, 0, MPI_REPLACE, &queue->win_row));
;

        printf("Rank %d announced completed row %d after enqueuing: %d at (row: %d, col: %d)\n", 
               mpi_get_rank(&queue->mpi_ctx), queue->eng_row - 1, value, queue->eng_row, queue->tail);
    } else {
        printf("Rank %d enqueued: %d (row: %d, col: %d)\n", 
               mpi_get_rank(&queue->mpi_ctx), value, queue->eng_row, queue->tail);
    }
    
    // Increment tail
    queue->tail++;
    return MPI_SUCCESS;
}

int spmc_queue_dequeue(spmc_queue_t *queue) {
    if (spmc_queue_is_enqueuer(queue)) return -1;
    
    int deq_row, head, val;
    
    // Step 1: AtomicRead ROW - MPI_Fetch_and_op with MPI_NO_OP
    int zero = 0;
    MPI_TRY(mpi_fetch_and_op(&zero, &deq_row, MPI_INT, 0, 0, MPI_NO_OP, &queue->win_row));
    
    // Check if there's actually a completed row available
    if (deq_row < 0) {
        printf("Rank %d: No completed rows available yet (deq_row=%d)\n", 
               mpi_get_rank(&queue->mpi_ctx), deq_row);
        return -1;
    }
    
    // Step 2: FetchInc HEAD[deq_row] - MPI_Fetch_and_op with MPI_SUM
    int one = 1;
    size_t head_element_offset = deq_row;
    size_t head_byte_offset = head_element_offset * sizeof(int);

    // Bounds checking
    if (head_element_offset >= MAX_ROWS) {
        fprintf(stderr, "FATAL: Head element offset %zu exceeds MAX_ROWS\n", head_element_offset);
        return -1;
    }

    // printf("[DEQ_OFFSET_DEBUG] Rank %d: Head element offset = %zu, byte offset = %zu for deq_row %d\n",
    //        mpi_get_rank(&queue->mpi_ctx), head_element_offset, head_byte_offset, deq_row);

    MPI_TRY(mpi_fetch_and_op(&one, &head, MPI_INT, 0, head_byte_offset, MPI_SUM, &queue->win_head));

    // Step 3: AtomicSwap ITEMS[deq_row, head] with T - MPI_Fetch_and_op with MPI_REPLACE
    int t_value = T; 
    size_t items_element_offset = (deq_row * MAX_COLS + head);
    size_t items_byte_offset = items_element_offset * sizeof(int);

    // Bounds checking
    if (items_element_offset >= MAX_ROWS * MAX_COLS) {
        fprintf(stderr, "FATAL: Items element offset %zu exceeds bounds\n", items_element_offset);
        return -1;
    }

    // printf("[DEQ_OFFSET_DEBUG] Rank %d: Items element offset = %zu, byte offset = %zu for (row:%d, col:%d)\n",
    //        mpi_get_rank(&queue->mpi_ctx), items_element_offset, items_byte_offset, deq_row, head);

    MPI_TRY(mpi_fetch_and_op(&t_value, &val, MPI_INT, 0, items_byte_offset, MPI_REPLACE, &queue->win_items));
    // MPI_TRY(mpi_win_flush(0, &queue->win_items));
    
    // Step 4: Check what we got
    if (val == L) { 
        printf("Rank %d found empty cell (val=%d) at (row: %d, col: %d)\n", 
               mpi_get_rank(&queue->mpi_ctx), val, deq_row, head);
        return -1;
    } else {
        printf("Rank %d dequeued: %d (row: %d, col: %d)\n", 
               mpi_get_rank(&queue->mpi_ctx), val, deq_row, head);
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