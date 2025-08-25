#ifndef SPMC_QUEUE_H
#define SPMC_QUEUE_H

#include <stdbool.h>
#include <stdint.h>
#include "mpi_lib.h"

// Constants for FFQ algorithm
#define EMPTY_CELL -1
#define DEQUEUED_CELL -2
#define MAX_ROW_SIZE 4000
#define MAX_NUM_ROWS 3
#define MAX_TRY_COUNT 3
#define MAX_LOG_SIZE 1024

typedef struct {
    int rank;        // Producer rank
    int gap;         // Gap for ordering
    int data;        // Actual data (integer)
    int row_epoch;   // Row epoch
} spmc_cell_t;

typedef struct{
    int rows, cols;
    int words_per_row;
    uint64_t *data;
} BitLog_t;

typedef struct {
    mpi_context_t mpi_ctx; // MPI context for communication

    spmc_cell_t **cells; // Pointer to cells array
    int* heads;
    int* row_epochs;
    BitLog_t* producer_log;
    BitLog_t* consumer_log;

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

//self implement
void bitlog_init(BitLog_t *log, int rows, int words_per_row);
void bitlog_destroy(BitLog_t *log);
int check_bit(uint64_t *log, int col);
#endif
