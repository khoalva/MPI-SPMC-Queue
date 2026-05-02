#ifndef SPMC_QUEUE_H
#define SPMC_QUEUE_H

#include <stdbool.h>
#include "mpi_lib.h"

// Constants for Circular Queue algorithm
#define MAX_QUEUE_SIZE 1000000

typedef struct {
    mpi_context_t mpi_ctx; // MPI context for communication

    int *data;          // Pointer to data array (circular buffer)
    int head;           // Local head index
    int tail;           // Local tail index  
    int reserved_head;  // Reserved head for synchronization
    int reserved_tail;  // Reserved tail for synchronization
    int head_buf;       // Cached head value for producer
    int tail_buf;       // Cached tail value for consumer
    int size;           // Queue capacity
    
    mpi_window_t win_data;          // Window for data array
    mpi_window_t win_tail;          // Window for tail
    mpi_window_t win_head;          // Window for head  
    mpi_window_t win_reserved_head; // Window for reserved_head
    mpi_window_t win_reserved_tail; // Window for reserved_tail
} spmc_queue_t;


// Bắt buộc cho benchmark chung
int spmc_queue_init(spmc_queue_t *queue, int argc, char *argv[]);
void spmc_queue_destroy(spmc_queue_t *queue);
int spmc_queue_enqueue(spmc_queue_t *queue, int value);
int spmc_queue_enqueue_batch(spmc_queue_t *queue, int *values, int count);
int spmc_queue_dequeue(spmc_queue_t *queue, int *out_data, int max_count);
void spmc_queue_print_stats(spmc_queue_t *queue);
int spmc_queue_is_enqueuer(spmc_queue_t *queue);
int spmc_queue_get_batch_size(spmc_queue_t *queue);
size_t spmc_queue_get_capacity_bytes(spmc_queue_t *queue);
#endif
