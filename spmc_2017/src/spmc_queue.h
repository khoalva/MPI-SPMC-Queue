#ifndef SPMC_QUEUE_H
#define SPMC_QUEUE_H

#include <stdbool.h>
#include "mpi_lib.h"

// Constants for FFQ algorithm
#define EMPTY_CELL -1
#define DEQUEUED_CELL -2
#define MAX_QUEUE_SIZE 1024

typedef struct {
    int rank;        // Producer rank
    int gap;         // Gap for ordering
    int data;        // Actual data (integer)
} spmc_cell_t;

typedef struct {
    int size;                // Queue size
    int head;               // Head pointer
    int tail;               // Tail pointer  
    int lastItemDequeued;   // Last dequeued item index
    mpi_context_t mpi_ctx; // MPI context for communication
    spmc_cell_t cells[];    // Flexible array member for cells
} spmc_queue_t;

// Producer operations (Single Producer)
bool spmc_enqueue(spmc_queue_t *queue, int item, mpi_window_t *win);

// Consumer operations (Multiple Consumers) 
bool spmc_dequeue(spmc_queue_t *queue, int consumer_id, int *item, mpi_window_t *win);

// Utility functions
void spmc_simulate_work(int time_ms);
bool spmc_is_producer(mpi_context_t *ctx);

// Bắt buộc cho benchmark chung
int spmc_queue_init(spmc_queue_t *queue, int argc, char *argv[]);
void spmc_queue_destroy(spmc_queue_t *queue);
int spmc_queue_enqueue(spmc_queue_t *queue, int value);
int spmc_queue_dequeue(spmc_queue_t *queue);
void spmc_queue_print_stats(spmc_queue_t *queue);
int spmc_queue_is_enqueuer(spmc_queue_t *queue);

#endif
