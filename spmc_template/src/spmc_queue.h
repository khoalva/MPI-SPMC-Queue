#ifndef SPMC_QUEUE_H
#define SPMC_QUEUE_H

#include <stdbool.h>
#include "mpi_lib.h"

// Example for queue you can modify it
typedef struct {
    int head;        // Head pointer
    int tail;        // Tail pointer
    int size;        // Queue size
    int lastItemDequeued; // Last dequeued item index
    int* cells; // Pointer to cells array
} queue_t;


typedef struct {
    mpi_context_t mpi_ctx; // Must have MPI context for communication
    queue_t q; // Queue metadata

    mpi_window_t win_queue; // Window for queue operations
} spmc_queue_t;


// Must have for benchmark compatibility
int spmc_queue_init(spmc_queue_t *queue, int argc, char *argv[]);
void spmc_queue_destroy(spmc_queue_t *queue);
int spmc_queue_enqueue(spmc_queue_t *queue, int value);
int spmc_queue_dequeue(spmc_queue_t *queue);
void spmc_queue_print_stats(spmc_queue_t *queue);
int spmc_queue_is_enqueuer(spmc_queue_t *queue);
size_t spmc_queue_get_capacity_bytes(const spmc_queue_t *queue);
// Can add more utility functions and structs as needed
#endif
