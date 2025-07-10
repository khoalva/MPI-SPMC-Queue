#define _POSIX_C_SOURCE 199309L
#define _GNU_SOURCE  // Add this for broader usleep support
#include "spmc_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

// Queue creation with proper memory allocation using mpi_lib
spmc_queue_t *spmc_queue_create(int size, mpi_window_t *win, mpi_context_t *ctx) {
    if (size <= 0 || size > MAX_QUEUE_SIZE) {
        fprintf(stderr, "Invalid queue size: %d\n", size);
        return NULL;
    }
    
    size_t queue_size = sizeof(spmc_queue_t) + size * sizeof(spmc_cell_t);
    spmc_queue_t *queue = NULL;
    
    // Only producer (rank 0) allocates memory
    if (spmc_is_producer(ctx)) {
        queue = mpi_malloc(queue_size, 0, mpi_get_rank(ctx));
        if (!queue) {
            fprintf(stderr, "Failed to allocate queue memory\n");
            return NULL;
        }
        
        // Initialize queue metadata
        queue->size = size;
        queue->head = 0;
        queue->tail = 0;
        queue->lastItemDequeued = -1;
        
        // Initialize all cells as empty
        for (int i = 0; i < size; i++) {
            queue->cells[i].rank = EMPTY_CELL;
            queue->cells[i].gap = 0;
            queue->cells[i].data = EMPTY_CELL;
        }
        
        printf("Rank %d: Queue created with size %d\n", mpi_get_rank(ctx), size);
    }
    
    // Create MPI window for shared access
    int result = mpi_win_create(queue, 
                               spmc_is_producer(ctx) ? queue_size : 0,
                               1,  // Use byte granularity instead of sizeof(spmc_cell_t)
                               ctx->comm, 
                               win);
    
    if (result != MPI_SUCCESS) {
        fprintf(stderr, "Failed to create MPI window\n");
        if (queue) mpi_free(queue, 0, mpi_get_rank(ctx));
        return NULL;
    }
    
    // Synchronize all processes
    mpi_barrier(ctx->comm);
    
    // For consumers, they need to access the queue through the window
    if (!spmc_is_producer(ctx)) {
        printf("Rank %d: Consumer initialized, will access queue through MPI window\n", mpi_get_rank(ctx));
        // Return a dummy non-NULL pointer to indicate success
        return (spmc_queue_t *)0x1;  // Non-NULL dummy pointer
    }
    
    return queue;
}

/**
 * @brief Destroy SPMC queue and cleanup resources
 * @param win MPI window to cleanup
 */
void spmc_queue_destroy(mpi_window_t *win) {
    if (win && win->is_valid) {
        mpi_win_destroy(win);
    }
}

/**
 * @brief Enqueue operation for single producer
 * @param queue Pointer to the queue (for producer) or dummy pointer (for consumers)
 * @param item Data item to enqueue (int)
 * @param win MPI window for communication
 * @param ctx MPI context
 * @return true if successful, false otherwise
 */
bool spmc_enqueue(spmc_queue_t *queue, int item, mpi_window_t *win, mpi_context_t *ctx) {
    if (!queue || !win || !ctx) {
        return false;
    }

    // Only producer should enqueue
    if (!spmc_is_producer(ctx)) {
        return false;
    }

    // Start passive target epoch for producer
    int lock_result = mpi_win_lock(MPI_LOCK_EXCLUSIVE, 0, 0, win);
    if (lock_result != MPI_SUCCESS) {
        return false;
    }
    
    int current_tail = queue->tail;
    int next_tail = (current_tail + 1) % queue->size;
    
    // Check if queue is full
    if (next_tail == queue->head) {
        mpi_win_unlock(0, win);
        return false; // Queue is full
    }
    
    // Fill the cell (producer can access directly)
    queue->cells[current_tail].rank = mpi_get_rank(ctx);
    queue->cells[current_tail].gap = 0;
    queue->cells[current_tail].data = item;
    
    // Update tail pointer
    queue->tail = next_tail;
    
    // End passive target epoch
    mpi_win_unlock(0, win);
    return true;
}

/**
 * @brief Dequeue operation for multiple consumers
 * @param queue Pointer to the queue (for producer) or dummy pointer (for consumers)
 * @param consumer_id ID of the consumer (for future use/tracking)
 * @param item Pointer to store dequeued int
 * @param win MPI window for communication
 * @param ctx MPI context
 * @return true if successful, false if queue is empty
 */
bool spmc_dequeue(spmc_queue_t *queue, int consumer_id, int *item, mpi_window_t *win, mpi_context_t *ctx) {
    if (!queue || !item || !win || !ctx) {
        return false;
    }

    // Suppress unused parameter warning
    (void)consumer_id;

    // Start passive target epoch for this consumer
    int lock_result = mpi_win_lock(MPI_LOCK_SHARED, 0, 0, win);
    if (lock_result != MPI_SUCCESS) {
        return false;
    }
    
    // For consumers, we need to access queue data through MPI window
    spmc_queue_t local_queue;
    if (!spmc_is_producer(ctx)) {
        // Consumer: Get queue metadata from producer (rank 0)
        int result = mpi_get(&local_queue, sizeof(spmc_queue_t), MPI_BYTE, 0, 0, win);
        if (result != MPI_SUCCESS) {
            mpi_win_unlock(0, win);
            return false;
        }
        queue = &local_queue;
    }
    
    // Check if queue is empty
    if (queue->head == queue->tail) {
        mpi_win_unlock(0, win);
        return false; // Queue is empty
    }
    
    int current_head = queue->head;
    
    // For consumers, get the cell data from producer's memory
    spmc_cell_t cell;
    if (!spmc_is_producer(ctx)) {
        size_t cell_offset = sizeof(spmc_queue_t) + current_head * sizeof(spmc_cell_t);
        int result = mpi_get(&cell, sizeof(spmc_cell_t), MPI_BYTE, 0, cell_offset, win);
        if (result != MPI_SUCCESS) {
            mpi_win_unlock(0, win);
            return false;
        }
    } else {
        // Producer can access directly
        cell = queue->cells[current_head];
    }
    
    // Check if this item was already dequeued
    if (cell.rank == EMPTY_CELL) {
        mpi_win_unlock(0, win);
        return false;
    }
    
    // Copy the data
    *item = cell.data;
    
    // Mark cell as consumed and update queue state
    cell.rank = EMPTY_CELL;
    cell.gap++;
    
    // Update the cell in producer's memory
    if (!spmc_is_producer(ctx)) {
        size_t cell_offset = sizeof(spmc_queue_t) + current_head * sizeof(spmc_cell_t);
        int result = mpi_put(&cell, sizeof(spmc_cell_t), MPI_BYTE, 0, cell_offset, win);
        if (result != MPI_SUCCESS) {
            mpi_win_unlock(0, win);
            return false;
        }
    } else {
        // Producer can update directly
        queue->cells[current_head] = cell;
    }
    
    // Update head pointer and lastItemDequeued
    int new_head = (current_head + 1) % queue->size;
    if (!spmc_is_producer(ctx)) {
        // Update queue metadata in producer's memory
        spmc_queue_t updated_queue = *queue;
        updated_queue.head = new_head;
        updated_queue.lastItemDequeued = current_head;
        
        int result = mpi_put(&updated_queue, sizeof(spmc_queue_t), MPI_BYTE, 0, 0, win);
        if (result != MPI_SUCCESS) {
            mpi_win_unlock(0, win);
            return false;
        }
    } else {
        // Producer can update directly
        queue->head = new_head;
        queue->lastItemDequeued = current_head;
    }
    
    // End passive target epoch
    mpi_win_unlock(0, win);
    return true;
}

/**
 * @brief Simulate work for testing purposes
 * @param time_ms Time to sleep in milliseconds
 */
void spmc_simulate_work(int time_ms) {
    if (time_ms <= 0) return;
    
    // Use usleep which takes microseconds (1 ms = 1000 microseconds)
    usleep(time_ms * 1000);
}

/**
 * @brief Check if the current context is the producer
 * @param ctx MPI context
 * @return true if producer, false otherwise
 */
bool spmc_is_producer(mpi_context_t *ctx) {
    return mpi_get_rank(ctx) == 0;
}