#ifndef SPMC_QUEUE_H
#define SPMC_QUEUE_H

#include <stdbool.h>
#include "mpi_lib.h"

// Constants for FFQ algorithm
#define EMPTY_CELL -1
#define DEQUEUED_CELL -2
#define MAX_ROW_SIZE 1024
#define MAX_NUM_ROWS 16
#define MAX_TRY_COUNT 3

typedef struct {
    int rank;        // Producer rank
    int gap;         // Gap for ordering
    int data;        // Actual data (integer)
} spmc_cell_t;

typedef struct {
    mpi_context_t mpi_ctx; // MPI context for communication

    spmc_cell_t **cells; // Pointer to cells array
    int* heads;
    int* row_epochs;
    int** producer_log;
    int** consumer_log;

    int row;
    int row_epoch;
    int tail;
    int try_count;
    int row_size;
    int num_row;

    mpi_window_t win_cells;
    mpi_window_t win_heads;
    mpi_window_t win_row_epochs;
    mpi_window_t win_producer_log;
    mpi_window_t win_consumer_log;
} spmc_queue_t;



// Bắt buộc cho benchmark chung
int spmc_queue_init(spmc_queue_t *queue, int argc, char *argv[]);
void spmc_queue_destroy(spmc_queue_t *queue);
int spmc_queue_enqueue(spmc_queue_t *queue, int value);
int spmc_queue_dequeue(spmc_queue_t *queue);
void spmc_queue_print_stats(spmc_queue_t *queue);
int spmc_queue_is_enqueuer(spmc_queue_t *queue);
size_t spmc_queue_get_capacity_bytes(spmc_queue_t *queue);
#endif
