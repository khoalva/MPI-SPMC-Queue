#include "spmc_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
                               sizeof(spmc_cell_t), 
                               ctx->comm, 
                               win);
    
    if (result != MPI_SUCCESS) {
        fprintf(stderr, "Failed to create MPI window\n");
        if (queue) mpi_free(queue, mpi_get_rank(ctx));
        return NULL;
    }
    
    // Synchronize all processes
    mpi_barrier(ctx);
    
    return queue;
}
    
    // Initialize queue on rank 0, then broadcast
    if (ctx->rank == 0) {
        queue->size = size;
        queue->head = 0;
        queue->tail = 0;
        queue->lastItemDequeued = -1;
        
        // Initialize all cells as empty
        for (int i = 0; i < size; i++) {
            queue->cells[i].rank = EMPTY_CELL;
            queue->cells[i].gap = 0;
            memset(&queue->cells[i].data, 0, sizeof(WeatherData));
        }
    }
    
    // Synchronize all processes
    mpi_barrier(ctx->comm);
    
    return queue;
}

/**
 * @brief Destroy SPMC queue and cleanup resources
 * @param win MPI window to cleanup
 */
void spmc_queue_destroy(mpi_window_t *win) {
    if (win && win->is_valid) {
        mpi_window_free(win);
    }
}

/**
 * @brief Enqueue operation for single producer
 * @param queue Pointer to the queue
 * @param item Data item to enqueue
 * @param win MPI window for communication
 * @return true if successful, false otherwise
 */
bool spmc_enqueue(spmc_queue_t *queue, WeatherData item, mpi_window_t *win) {
    if (!queue || !win) {
        return false;
    }

    mpi_window_fence(0, win);
    
    int current_tail = queue->tail;
    int next_tail = (current_tail + 1) % queue->size;
    
    // Check if queue is full
    if (next_tail == queue->head) {
        mpi_window_fence(0, win);
        return false; // Queue is full
    }
    
    // Get current rank for this process
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    
    // Fill the cell
    queue->cells[current_tail].rank = rank;
    queue->cells[current_tail].gap = 0;
    queue->cells[current_tail].data = item;
    
    // Update tail pointer
    queue->tail = next_tail;
    
    mpi_window_fence(0, win);
    return true;
}

/**
 * @brief Dequeue operation for multiple consumers
 * @param queue Pointer to the queue
 * @param consumer_id ID of the consumer
 * @param item Pointer to store dequeued item
 * @param win MPI window for communication
 * @return true if successful, false if queue is empty
 */
bool spmc_dequeue(spmc_queue_t *queue, int consumer_id, WeatherData *item, mpi_window_t *win) {
    if (!queue || !item || !win) {
        return false;
    }

    mpi_window_fence(0, win);
    
    // Check if queue is empty
    if (queue->head == queue->tail) {
        mpi_window_fence(0, win);
        return false; // Queue is empty
    }
    
    int current_head = queue->head;
    spmc_cell_t *cell = &queue->cells[current_head];
    
    // Check if this item was already dequeued
    if (cell->rank == EMPTY_CELL) {
        mpi_window_fence(0, win);
        return false;
    }
    
    // Copy the data
    *item = cell->data;
    
    // Mark cell as consumed by this consumer
    cell->rank = EMPTY_CELL;
    cell->gap++;
    
    // Update head pointer if this was the last consumer for this item
    // For simplicity, we advance head immediately (can be optimized)
    queue->head = (current_head + 1) % queue->size;
    queue->lastItemDequeued = current_head;
    
    mpi_window_fence(0, win);
    return true;
}

/**
 * @brief Simulate work for testing purposes
 * @param time_ms Time to sleep in milliseconds
 */
void spmc_simulate_work(int time_ms) {
    if (time_ms <= 0) return;
    
    struct timespec ts;
    ts.tv_sec = time_ms / 1000;
    ts.tv_nsec = (time_ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}
