#define _POSIX_C_SOURCE 199309L
#define _GNU_SOURCE  // Add this for broader usleep support
#include "spmc_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

mpi_window_t global_win;


/**
 * @brief Enqueue operation for single producer
 * @param queue Pointer to the queue (for producer) or dummy pointer (for consumers)
 * @param item Data item to enqueue (int)
 * @param win MPI window for communication
 * @param ctx MPI context
 * @return true if successful, false otherwise
 */
bool spmc_enqueue(spmc_queue_t *queue, int item, mpi_window_t *win) {
    if (!queue || !win) {
        return false;
    }

    // Only producer should enqueue
    if (!spmc_is_producer(&queue->mpi_ctx)) {
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
    queue->cells[current_tail].rank = mpi_get_rank(&queue->mpi_ctx);
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
bool spmc_dequeue(spmc_queue_t *queue, int consumer_id, int *item, mpi_window_t *win) {
    if (!queue || !item || !win) {
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
    if (!spmc_is_producer(&queue->mpi_ctx)) {
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
    if (!spmc_is_producer(&queue->mpi_ctx)) {
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
    if (!spmc_is_producer(&queue->mpi_ctx)) {
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
    if (!spmc_is_producer(&queue->mpi_ctx)) {
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

// ==== BEGIN: Required API for benchmark compatibility ====

int spmc_queue_init(spmc_queue_t *queue, int argc, char *argv[]) {
    int size = 128;
    if (argc > 1) {
        int parsed = atoi(argv[1]);
        if (parsed > 0 && parsed <= MAX_QUEUE_SIZE) size = parsed;
    }

    queue->size = size;
    queue->head = 0;
    queue->tail = 0;
    queue->lastItemDequeued = -1;
    for (int i = 0; i < size; ++i) {
        queue->cells[i].rank = EMPTY_CELL;
        queue->cells[i].gap = 0;
        queue->cells[i].data = EMPTY_CELL;
    }

    // Tạo MPI window cho queue
    int result = mpi_win_create(
        queue,
        sizeof(spmc_queue_t) + size * sizeof(spmc_cell_t),
        1,
        queue->mpi_ctx.comm,
        &global_win
    );
    if (result != MPI_SUCCESS) {
        fprintf(stderr, "Failed to create MPI window\n");
        return -1;
    }
    mpi_barrier(queue->mpi_ctx.comm);
    return 0;
}

void spmc_queue_destroy(spmc_queue_t *queue) {
    extern mpi_window_t global_win;
    mpi_win_destroy(&global_win);
}

int spmc_queue_enqueue(spmc_queue_t *queue, int value) {
    extern mpi_window_t global_win;
    return spmc_enqueue(queue, value, &global_win) ? 0 : -1;
}

int spmc_queue_dequeue(spmc_queue_t *queue) {
    extern mpi_window_t global_win;
    int item = -1;
    bool ok = spmc_dequeue(queue, 0, &item, &global_win);
    return ok ? item : -1;
}

void spmc_queue_print_stats(spmc_queue_t *queue) {
    printf("Queue size: %d, head: %d, tail: %d\n", queue->size, queue->head, queue->tail);
}

int spmc_queue_is_enqueuer(spmc_queue_t *queue) {
    return spmc_is_producer(&queue->mpi_ctx) ? 1 : 0;
}
// ==== END: Required API for benchmark compatibility ====