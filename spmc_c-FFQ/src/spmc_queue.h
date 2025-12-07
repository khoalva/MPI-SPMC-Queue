#ifndef SPMC_QUEUE_H
#define SPMC_QUEUE_H

#include <stdbool.h>
#include "mpi_lib.h"

// Constants for FFQ algorithm
#define EMPTY_CELL -1
#define DEQUEUED_CELL -2
#define MAX_QUEUE_SIZE 1000000
#define BATCH_SIZE 10
#define MAX_DEQUEUE_RETRIES 1
#define MAX_WAIT_COUNT 1
typedef struct {
    mpi_context_t mpi_ctx; // MPI context for communication

    int *ranks;  // Array for producer ranks
    int *gaps;   // Array for gaps/ordering
    int *datas;  // Array for actual data
    int head;
    int tail;
    int size;
    
    int queue_owner_rank; // Rank where queue memory is allocated (default: 0)
    
    mpi_window_t win_ranks;
    mpi_window_t win_gaps;
    mpi_window_t win_datas;
    mpi_window_t win_head;
} spmc_queue_t;


// Bắt buộc cho benchmark chung
int spmc_queue_init(spmc_queue_t *queue, int argc, char *argv[]);
int spmc_queue_init_with_queue_owner(spmc_queue_t *queue, int argc, char *argv[], int queue_owner_rank);
void spmc_queue_destroy(spmc_queue_t *queue);
int spmc_queue_enqueue(spmc_queue_t *queue, int value);
int spmc_queue_dequeue(spmc_queue_t *queue, int *out_data, int max_count);
void spmc_queue_print_stats(spmc_queue_t *queue);
int spmc_queue_is_enqueuer(spmc_queue_t *queue);
int spmc_queue_get_batch_size(spmc_queue_t *queue);
size_t spmc_queue_get_capacity_bytes(spmc_queue_t *queue);
#endif
