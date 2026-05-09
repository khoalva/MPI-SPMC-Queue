#ifndef SPMC_QUEUE_H
#define SPMC_QUEUE_H

#include <stdbool.h>
#include "mpi_lib.h"

/* =========================================================================
 *  Naive Batch dFFQ (nbdFFQ)
 *
 *  Mapping: 1 slot (rank) → 1 batch of BATCH_SIZE items.
 *  Algorithm logic is identical to dFFQ, but the unit is a batch, not an
 *  item.  A single mpi_put/mpi_get transfers all BATCH_SIZE data values.
 *
 *  Data layout in the datas window:
 *    slot s  →  datas[s * BATCH_SIZE .. (s+1)*BATCH_SIZE - 1]
 * ========================================================================= */

// Constants for FFQ algorithm
#define EMPTY_CELL       -1
#define DEQUEUED_CELL    -2
#define MAX_QUEUE_SIZE   1048576   /* number of batch-slots in the ring */
#define BATCH_SIZE       32        /* items per batch-slot */
#define MAX_DEQUEUE_RETRIES 10
#define MAX_WAIT_COUNT      10

typedef struct {
    mpi_context_t mpi_ctx; // MPI context for communication

    int *ranks;  // Array[N]            — slot rank (−1 = empty)
    int *gaps;   // Array[N]            — gap markers
    int *datas;  // Array[N*BATCH_SIZE] — payload; stride = BATCH_SIZE

    int head;
    int tail;
    int size;   /* number of batch-slots, == MAX_QUEUE_SIZE */

    int queue_owner_rank;

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
int spmc_queue_enqueue_batch(spmc_queue_t *queue, int *values, int count);
int spmc_queue_dequeue(spmc_queue_t *queue, int *out_data, int max_count);
void spmc_queue_print_stats(spmc_queue_t *queue);
int spmc_queue_is_enqueuer(spmc_queue_t *queue);

/* Separate batch-size queries for producer and consumers */
int spmc_queue_get_enq_batch_size(spmc_queue_t *queue);
int spmc_queue_get_deq_batch_size(spmc_queue_t *queue);

/* Legacy alias — returns BATCH_SIZE */
int spmc_queue_get_batch_size(spmc_queue_t *queue);

size_t spmc_queue_get_capacity_bytes(spmc_queue_t *queue);
#endif
