#include "queue_wrapper.h"
#include <stdio.h>
#include <stdlib.h>

// Initialize the queue with MPI wrapper
int queue_wrapper_init(spmc_queue_t *queue, int argc, char *argv[]) {
    if (!queue) return MPI_ERR_ARG;
    
    // Initialize MPI context
    int err = mpi_init(argc, argv, &queue->mpi_ctx);
    if (err != MPI_SUCCESS) return err;
    
    // Check minimum number of processes
    if (queue->mpi_ctx.size < 2) {
        fprintf(stderr, "At least two processes are required\n");
        mpi_finalize();
        return MPI_ERR_OTHER;
    }
    
    // Initialize queue data structures
    queue->row = 0;
    queue->eng_row = 0;
    queue->tail = 0;
    
    // Allocate memory on root process
    if (mpi_is_root(&queue->mpi_ctx)) {
        queue->head = (int *)mpi_malloc(MAX_ROWS * sizeof(int), 0, queue->mpi_ctx.rank);
        queue->items = (int *)mpi_malloc(MAX_ROWS * MAX_COLS * sizeof(int), 0, queue->mpi_ctx.rank);
        
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
    } else {
        queue->head = NULL;
        queue->items = NULL;
    }
    
    // Create MPI windows using wrapper
    err = mpi_win_create(queue->head, 
                        mpi_is_root(&queue->mpi_ctx) ? MAX_ROWS * sizeof(int) : 0,
                        sizeof(int), queue->mpi_ctx.comm, &queue->win_head);
    if (err != MPI_SUCCESS) return err;
    
    err = mpi_win_create(queue->items, 
                        mpi_is_root(&queue->mpi_ctx) ? MAX_ROWS * MAX_COLS * sizeof(int) : 0,
                        sizeof(int), queue->mpi_ctx.comm, &queue->win_items);
    if (err != MPI_SUCCESS) return err;
    
    err = mpi_win_create(&queue->row, 
                        mpi_is_root(&queue->mpi_ctx) ? sizeof(int) : 0,
                        sizeof(int), queue->mpi_ctx.comm, &queue->win_row);
    if (err != MPI_SUCCESS) return err;
    
    // Lock all windows for passive target synchronization
    mpi_window_t windows[] = {queue->win_head, queue->win_items, queue->win_row};
    err = mpi_win_lock_all_multiple(windows, 3);
    if (err != MPI_SUCCESS) return err;
    
    printf("Queue initialized successfully on rank %d/%d\n", 
           queue->mpi_ctx.rank, queue->mpi_ctx.size);
    
    return MPI_SUCCESS;
}

// Cleanup queue resources
void queue_wrapper_cleanup(spmc_queue_t *queue) {
    if (!queue) return;
    
    // Unlock all windows
    mpi_window_t windows[] = {queue->win_head, queue->win_items, queue->win_row};
    mpi_unlock_all_windows(windows, 3);
    
    // Destroy windows
    mpi_win_destroy(&queue->win_head);
    mpi_win_destroy(&queue->win_items);
    mpi_win_destroy(&queue->win_row);
    
    // Free memory
    mpi_free(queue->head, 0, queue->mpi_ctx.rank);
    mpi_free(queue->items, 0, queue->mpi_ctx.rank);
    
    // Finalize MPI
    mpi_finalize();
    
    printf("Queue cleaned up on rank %d\n", queue->mpi_ctx.rank);
}

// Enqueue operation using wrapper
int queue_wrapper_enqueue(spmc_queue_t *queue, int value) {
    if (!queue) return -1;
    
    // Only rank 0 can enqueue
    if (!queue_wrapper_is_enqueuer(queue)) {
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
    int err = mpi_compare_and_swap(&value, &l_value, &val, MPI_INT, 
                                   0, target_offset, &queue->win_items);
    if (err != MPI_SUCCESS) return -1;
    
    if (val == T) {
        // Dequeuer accessed this cell; move to next row
        queue->eng_row++;
        queue->tail = 0;
        
        if (queue->eng_row >= MAX_ROWS) {
            fprintf(stderr, "Row limit exceeded\n");
            return -1;
        }
        
        // Update ROW to new row
        err = mpi_put(&queue->eng_row, 1, MPI_INT, 0, 0, &queue->win_row);
        if (err != MPI_SUCCESS) return -1;
        
        // Swap ITEMS[eng_row, tail] with value (should return L)
        target_offset = (queue->eng_row * MAX_COLS + queue->tail) * sizeof(int);
        err = mpi_compare_and_swap(&value, &l_value, &val, MPI_INT, 
                                   0, target_offset, &queue->win_items);
        if (err != MPI_SUCCESS) return -1;
    }
    
    queue->tail++;
    printf("Rank %d enqueued: %d (row: %d, col: %d)\n", 
           queue->mpi_ctx.rank, value, queue->eng_row, queue->tail - 1);
    
    return MPI_SUCCESS;
}

// Dequeue operation using wrapper
int queue_wrapper_dequeue(spmc_queue_t *queue) {
    if (!queue) return -1;
    
    int deq_row, head_val, val;
    int l_value = L;  // ⊥
    int t_value = T;  // ⊤
    
    // Step 1: Read ROW
    int err = mpi_get(&deq_row, 1, MPI_INT, 0, 0, &queue->win_row);
    if (err != MPI_SUCCESS) return -1;
    
    // Step 2: Fetch&Add HEAD[deq_row]
    int one = 1;
    size_t head_offset = deq_row * sizeof(int);
    err = mpi_fetch_and_op(&one, &head_val, MPI_INT, 0, head_offset, 
                           MPI_SUM, &queue->win_head);
    if (err != MPI_SUCCESS) return -1;
    
    // Step 3: Swap ITEMS[deq_row, head] with T
    size_t items_offset = (deq_row * MAX_COLS + head_val) * sizeof(int);
    err = mpi_compare_and_swap(&t_value, &l_value, &val, MPI_INT, 
                               0, items_offset, &queue->win_items);
    if (err != MPI_SUCCESS) return -1;
    
    // Return value or -1 if queue is empty
    if (val != T && val != L) {
        printf("Rank %d dequeued: %d (row: %d, col: %d)\n", 
               queue->mpi_ctx.rank, val, deq_row, head_val);
        return val;
    } else {
        printf("Rank %d found empty queue (row: %d, col: %d)\n", 
               queue->mpi_ctx.rank, deq_row, head_val);
        return -1;
    }
}

// Print queue statistics
void queue_wrapper_print_stats(spmc_queue_t *queue) {
    if (!queue) return;
    
    printf("Queue stats for rank %d:\n", queue->mpi_ctx.rank);
    printf("  MPI size: %d\n", queue->mpi_ctx.size);
    printf("  Current row: %d\n", queue->row);
    
    if (queue_wrapper_is_enqueuer(queue)) {
        printf("  Enqueuer row: %d\n", queue->eng_row);
        printf("  Enqueuer tail: %d\n", queue->tail);
    }
}

// Check if current process is the enqueuer
int queue_wrapper_is_enqueuer(spmc_queue_t *queue) {
    return queue && mpi_is_root(&queue->mpi_ctx);
}
