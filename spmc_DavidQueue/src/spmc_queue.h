#ifndef SPMC_QUEUE_H
#define SPMC_QUEUE_H

#include "mpi_lib.h"

// Constants for special values
#define L -1 // ⊥ (empty cell)
#define T -2 // ⊤ (dequeued cell)
#define MAX_ROWS 5 // Maximum rows in ITEMS
#define MAX_COLS 110000 // Maximum columns in ITEMS
#define MAX_VALUE 1024 // Maximum value to enqueue

// Queue structure using MPI wrapper library
typedef struct {
    mpi_context_t mpi_ctx;
    int *head;
    int *items;
    int row;
    
    int queue_owner_rank; // Rank where queue memory is allocated (default: 0)
    
    mpi_window_t win_head;
    mpi_window_t win_items;
    mpi_window_t win_row;
    int eng_row;  // Enqueuer's current row
    int tail;     // Enqueuer's current tail position
} spmc_queue_t;

// Function prototypes
int spmc_queue_init(spmc_queue_t *queue, int argc, char *argv[]);
int spmc_queue_init_with_queue_owner(spmc_queue_t *queue, int argc, char *argv[], int queue_owner_rank);
void spmc_queue_destroy(spmc_queue_t *queue);
int spmc_queue_enqueue(spmc_queue_t *queue, int value);
int spmc_queue_dequeue(spmc_queue_t *queue, int *out_data, int max_count);
void spmc_queue_print_stats(spmc_queue_t *queue);
int spmc_queue_is_enqueuer(spmc_queue_t *queue);
int spmc_queue_get_batch_size(spmc_queue_t *queue);
size_t spmc_queue_get_capacity_bytes(const spmc_queue_t *queue);
#endif // SPMC_QUEUE_H
