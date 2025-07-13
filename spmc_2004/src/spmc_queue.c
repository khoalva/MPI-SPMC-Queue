#include "spmc_queue.h"
#include <stdio.h>
#include <stdlib.h>

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
    
    // Initialize queue data structures
    queue->row = 0;
    queue->eng_row = 0;
    queue->tail = 0;
    
    // Allocate memory using the new library
    queue->head = mpi_calloc(MAX_ROWS * sizeof(int), 0, mpi_get_rank(&queue->mpi_ctx));
    queue->items = mpi_calloc(MAX_ROWS * MAX_COLS * sizeof(int), 0, mpi_get_rank(&queue->mpi_ctx));
    
    if (mpi_is_root(&queue->mpi_ctx)) {
        if (!queue->head || !queue->items) {
            fprintf(stderr, "Memory allocation failed\n");
            return MPI_ERR_OTHER;
        }
        
        // Initialize all items to L (⊥)
        for (int i = 0; i < MAX_ROWS * MAX_COLS; i++) {
            queue->items[i] = L;
        }
        
        // Initialize head array to 0
        for (int i = 0; i < MAX_ROWS; i++) {
            queue->head[i] = 0;
        }
    }
    
    // Create MPI windows using the new library
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
    
    // Unlock all windows
    mpi_window_t windows[] = {queue->win_head, queue->win_items, queue->win_row};
    mpi_win_unlock_all_multiple(windows, 3);
    
    // Destroy windows
    mpi_win_destroy(&queue->win_head);
    mpi_win_destroy(&queue->win_items);
    mpi_win_destroy(&queue->win_row);
    
    // Free memory
    mpi_free(queue->head, 0, mpi_get_rank(&queue->mpi_ctx));
    mpi_free(queue->items, 0, mpi_get_rank(&queue->mpi_ctx));
    
    // Finalize MPI
    mpi_finalize();
    
    printf("SPMC Queue destroyed on rank %d\n", mpi_get_rank(&queue->mpi_ctx));
}

int spmc_queue_enqueue(spmc_queue_t *queue, int value) {
    if (!queue) return -1;
    
    // Only rank 0 can enqueue
    if (!spmc_queue_is_enqueuer(queue)) {
        fprintf(stderr, "Only rank 0 can enqueue\n");
        return -1;
    }
    
    // Validate input
    if (value < 0 || value > MAX_VALUE) {
        fprintf(stderr, "Invalid enqueue value: %d (must be 0-%d)\n", value, MAX_VALUE);
        return -1;
    }
    
    int val;
    int l_value = L;  // ⊥
    size_t target_offset = (queue->eng_row * MAX_COLS + queue->tail) * sizeof(int);
    
    // Step 1: Swap ITEMS[eng_row, tail] with value
    MPI_TRY(mpi_compare_and_swap(&value, &l_value, &val, MPI_INT, 
                                 0, target_offset, &queue->win_items));
    
    if (val == T) {
        // Dequeuer accessed this cell; move to next row
        queue->eng_row++;
        queue->tail = 0;
        
        if (queue->eng_row >= MAX_ROWS) {
            fprintf(stderr, "Row limit exceeded\n");
            return -1;
        }
        
        // Update ROW to new row
        MPI_TRY(mpi_put(&queue->eng_row, 1, MPI_INT, 0, 0, &queue->win_row));
        
        // Swap ITEMS[eng_row, tail] with value (should return L)
        target_offset = (queue->eng_row * MAX_COLS + queue->tail) * sizeof(int);
        MPI_TRY(mpi_compare_and_swap(&value, &l_value, &val, MPI_INT, 
                                     0, target_offset, &queue->win_items));
    }
    
    queue->tail++;
    printf("Rank %d enqueued: %d (row: %d, col: %d)\n", 
           mpi_get_rank(&queue->mpi_ctx), value, queue->eng_row, queue->tail - 1);
    
    return MPI_SUCCESS;
}

int spmc_queue_dequeue(spmc_queue_t *queue) {
    if (!queue) return -1;
    
    int deq_row, head_val, val;
    int l_value = L;  // ⊥
    int t_value = T;  // ⊤
    
    // Step 1: Read ROW
    MPI_TRY(mpi_get(&deq_row, 1, MPI_INT, 0, 0, &queue->win_row));
    
    // Step 2: Fetch&Add HEAD[deq_row]
    int one = 1;
    size_t head_offset = deq_row * sizeof(int);
    MPI_TRY(mpi_fetch_and_op(&one, &head_val, MPI_INT, 0, head_offset, 
                             MPI_SUM, &queue->win_head));
    
    // Step 3: Swap ITEMS[deq_row, head] with T
    size_t items_offset = (deq_row * MAX_COLS + head_val) * sizeof(int);
    MPI_TRY(mpi_compare_and_swap(&t_value, &l_value, &val, MPI_INT, 
                                 0, items_offset, &queue->win_items));
    
    // Return value or -1 if queue is empty
    if (val != T && val != L) {
        printf("Rank %d dequeued: %d (row: %d, col: %d)\n", 
               mpi_get_rank(&queue->mpi_ctx), val, deq_row, head_val);
        return val;
    } else {
        printf("Rank %d found empty queue (row: %d, col: %d)\n", 
               mpi_get_rank(&queue->mpi_ctx), deq_row, head_val);
        return -1;
    }
}

void spmc_queue_print_stats(spmc_queue_t *queue) {
    if (!queue) return;
    
    printf("SPMC Queue stats for rank %d:\n", mpi_get_rank(&queue->mpi_ctx));
    printf("  MPI size: %d\n", mpi_get_size(&queue->mpi_ctx));
    printf("  Current row: %d\n", queue->row);
    
    if (spmc_queue_is_enqueuer(queue)) {
        printf("  Enqueuer row: %d\n", queue->eng_row);
        printf("  Enqueuer tail: %d\n", queue->tail);
    }
}

int spmc_queue_is_enqueuer(spmc_queue_t *queue) {
    return queue && mpi_is_root(&queue->mpi_ctx);
}

size_t spmc_queue_get_capacity_bytes(const spmc_queue_t *queue) {
    if (!queue) return 0;
    // Tổng dung lượng bộ nhớ cho queue: head + items + các biến metadata
    // head: MAX_ROWS * sizeof(int)
    // items: MAX_ROWS * MAX_COLS * sizeof(int)
    // metadata: row, eng_row, tail (3 int)
    size_t meta_bytes = 3 * sizeof(int);
    size_t head_bytes = MAX_ROWS * sizeof(int);
    size_t items_bytes = MAX_ROWS * MAX_COLS * sizeof(int);
    return meta_bytes + head_bytes + items_bytes;
}