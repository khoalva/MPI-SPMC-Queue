#define _POSIX_C_SOURCE 199309L
#define _GNU_SOURCE

#include "spmc_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/* =========================================================================
 *  Naive Batch dFFQ (nbdFFQ) — MPI RMA implementation
 *
 *  The algorithm is structurally identical to dFFQ, with one key difference:
 *
 *    dFFQ  : 1 rank (slot) → 1 item
 *    nbdFFQ: 1 rank (slot) → 1 batch of BATCH_SIZE items
 *
 *  The ring buffer has N = MAX_QUEUE_SIZE slots.  Each slot s holds:
 *    ranks[s]                               — slot rank sentinel (int)
 *    gaps[s]                                — gap marker (int)
 *    datas[s*BATCH_SIZE .. (s+1)*BATCH_SIZE-1] — BATCH_SIZE payload ints
 *
 *  Enqueue (producer only):
 *    - Iterates over slot tail % N exactly like dFFQ.
 *    - If ranks[slot] < 0 (empty): mpi_put the whole batch of k ints at once,
 *      then atomic-write ranks[slot] = tail.  tail++.
 *    - If ranks[slot] >= 0 (busy): atomic-write gaps[slot] = tail.  tail++.
 *
 *  Dequeue (any consumer):
 *    - FAA(head, 1) → rank  (claims one batch-slot)
 *    - Spin exactly like dFFQ until ranks[rank%N] == rank, then
 *      mpi_get all BATCH_SIZE ints in one call, then clear ranks[rank%N] = -1.
 *
 *  MPI operation mapping (same as bdFFQ):
 *    AtomicRead  → mpi_fetch_and_op (MPI_NO_OP)
 *    AtomicWrite → mpi_accumulate   (MPI_REPLACE)
 *    Put         → mpi_put          (non-atomic, bulk)
 *    Get         → mpi_get          (non-atomic, bulk)
 *    FAA         → mpi_fetch_and_op (MPI_SUM)
 * ========================================================================= */

/* =========================================================================
 *  Initialization / Destruction
 * ========================================================================= */

/**
 * @brief Initializes the SPMC queue with a configurable queue-owner rank.
 *
 * Only the queue-owner process allocates the ring-buffer memory and exposes
 * it via MPI windows.  All other processes contribute zero-size windows so
 * that passive-target RMA can proceed without explicit synchronisation on
 * the target side.
 */
int spmc_queue_init_with_queue_owner(spmc_queue_t *queue, int argc, char *argv[],
                                     int queue_owner_rank) {
    if (!queue) return -1;

    MPI_TRY(mpi_init(argc, argv, &queue->mpi_ctx));

    if (mpi_get_size(&queue->mpi_ctx) < 2) {
        mpi_finalize();
        return -1;
    }

    int rank = mpi_get_rank(&queue->mpi_ctx);
    int size = mpi_get_size(&queue->mpi_ctx);

    if (queue_owner_rank < 0 || queue_owner_rank >= size) {
        if (rank == 0)
            fprintf(stderr, "Invalid queue owner rank %d (must be 0-%d)\n",
                    queue_owner_rank, size - 1);
        mpi_finalize();
        return -1;
    }

    queue->queue_owner_rank = queue_owner_rank;
    queue->size = MAX_QUEUE_SIZE;   /* number of batch-slots */

    int is_queue_owner = (rank == queue_owner_rank);

    if (is_queue_owner) {
        /* Each slot stores BATCH_SIZE ints in datas, 1 int in ranks/gaps. */
        queue->ranks = malloc(queue->size * sizeof(int));
        queue->gaps  = malloc(queue->size * sizeof(int));
        queue->datas = malloc((size_t)queue->size * BATCH_SIZE * sizeof(int));

        if (!queue->ranks || !queue->gaps || !queue->datas) {
            if (queue->ranks) free(queue->ranks);
            if (queue->gaps)  free(queue->gaps);
            if (queue->datas) free(queue->datas);
            mpi_finalize();
            return -1;
        }

        queue->head = 0;
        queue->tail = 0;

        for (int i = 0; i < queue->size; i++) {
            queue->ranks[i] = EMPTY_CELL;  /* -1 = empty/available */
            queue->gaps[i]  = 0;
        }
        memset(queue->datas, 0, (size_t)queue->size * BATCH_SIZE * sizeof(int));
    } else {
        queue->ranks = NULL;
        queue->gaps  = NULL;
        queue->datas = NULL;
        queue->head  = 0;
        queue->tail  = 0;
    }

    /* Window sizes: 0 for non-owners */
    size_t slot_arr_size  = is_queue_owner ? (size_t)queue->size * sizeof(int) : 0;
    size_t datas_arr_size = is_queue_owner ? (size_t)queue->size * BATCH_SIZE * sizeof(int) : 0;
    size_t head_size      = is_queue_owner ? sizeof(int) : 0;

    MPI_TRY(mpi_win_create(queue->ranks, slot_arr_size,  sizeof(int),
                            queue->mpi_ctx.comm, &queue->win_ranks));
    MPI_TRY(mpi_win_create(queue->gaps,  slot_arr_size,  sizeof(int),
                            queue->mpi_ctx.comm, &queue->win_gaps));
    MPI_TRY(mpi_win_create(queue->datas, datas_arr_size, sizeof(int),
                            queue->mpi_ctx.comm, &queue->win_datas));
    MPI_TRY(mpi_win_create(&queue->head, head_size,      sizeof(int),
                            queue->mpi_ctx.comm, &queue->win_head));

    mpi_window_t windows[] = {queue->win_ranks, queue->win_gaps,
                               queue->win_datas, queue->win_head};
    MPI_TRY(mpi_win_lock_all_multiple(windows, 4));

    printf("nbdFFQ SPMC Queue initialized on rank %d/%d (queue_owner=%d, batch=%d)\n",
           rank, size, queue_owner_rank, BATCH_SIZE);

    return MPI_SUCCESS;
}

/* Backward-compatible wrapper: default queue owner = rank 0. */
int spmc_queue_init(spmc_queue_t *queue, int argc, char *argv[]) {
    return spmc_queue_init_with_queue_owner(queue, argc, argv, 0);
}

/**
 * @brief Destroys the queue and frees all MPI/memory resources.
 */
void spmc_queue_destroy(spmc_queue_t *queue) {
    if (!queue) return;

    mpi_window_t windows[] = {queue->win_ranks, queue->win_gaps,
                               queue->win_datas, queue->win_head};
    mpi_win_unlock_all_multiple(windows, 4);

    mpi_win_destroy(&queue->win_ranks);
    mpi_win_destroy(&queue->win_gaps);
    mpi_win_destroy(&queue->win_datas);
    mpi_win_destroy(&queue->win_head);

    if (mpi_get_rank(&queue->mpi_ctx) == queue->queue_owner_rank) {
        if (queue->ranks) free(queue->ranks);
        if (queue->gaps)  free(queue->gaps);
        if (queue->datas) free(queue->datas);
        queue->ranks = NULL;
        queue->gaps  = NULL;
        queue->datas = NULL;
    }

    mpi_finalize();
}

/* =========================================================================
 *  Batch Enqueue  (producer / queue-owner only)
 *
 *  Algorithm — same control flow as dFFQ enqueue, but the "item" is now a
 *  full batch of BATCH_SIZE values written with a single mpi_put:
 *
 *    BATCH_ENQ(data[0..k-1]):          // k must equal BATCH_SIZE
 *      success ← false
 *      while ¬success:
 *        slot ← tail mod N
 *        cell_rank ← AtomicRead(ranks[slot])
 *        if cell_rank < 0:             // slot is free
 *          Put(datas[slot*k .. (slot+1)*k-1], data[0..k-1])   // bulk put
 *          AtomicWrite(ranks[slot], tail)
 *          success ← true
 *        else:                         // slot is busy (gap)
 *          AtomicWrite(gaps[slot], tail)
 *        tail ← tail + 1
 * ========================================================================= */

/**
 * @brief Enqueues exactly BATCH_SIZE items as one atomic batch-slot.
 * @param values  Array of exactly BATCH_SIZE ints to enqueue.
 * @param count   Must equal BATCH_SIZE (ignored beyond that).
 * @return MPI_SUCCESS on success, -1 on error.
 */
int spmc_queue_enqueue_batch(spmc_queue_t *queue, int *values, int count) {
    if (!spmc_queue_is_enqueuer(queue)) return -1;
    if (!values || count <= 0) return -1;

    /* nbdFFQ always treats exactly BATCH_SIZE items as one indivisible unit. */
    int k = BATCH_SIZE;
    (void)count;  /* caller is expected to pass BATCH_SIZE */

    int target_rank = queue->queue_owner_rank;
    int N           = queue->size;  /* number of batch-slots */

    bool success = false;
    int  no_op_val = 0;

    while (!success) {
        int slot      = queue->tail % N;
        MPI_Aint rank_disp = (MPI_Aint)slot * sizeof(int);

        /* ---- AtomicRead(ranks[slot]) ---- */
        int cell_rank;
        MPI_TRY(mpi_fetch_and_op(&no_op_val, &cell_rank, MPI_INT,
                                  target_rank, rank_disp,
                                  MPI_NO_OP, &queue->win_ranks));

        if (cell_rank < 0) {
            /* Slot is free: write all k items in one mpi_put, then publish. */

            /* Put(datas[slot*k .. slot*k + k-1], values[0..k-1]) */
            MPI_Aint data_disp = (MPI_Aint)slot * k * sizeof(int);
            MPI_TRY(mpi_put(values, k * sizeof(int), MPI_BYTE,
                            target_rank, data_disp, &queue->win_datas));

            /* AtomicWrite(ranks[slot], tail) — publishes the batch */
            int tail_val = queue->tail;
            MPI_TRY(mpi_accumulate(&tail_val, 1, MPI_INT,
                                   target_rank, rank_disp,
                                   MPI_REPLACE, &queue->win_ranks));

            success = true;
        } else {
            /* Slot is busy: mark gap and move on. */
            MPI_Aint gap_disp = (MPI_Aint)slot * sizeof(int);
            int tail_val = queue->tail;
            MPI_TRY(mpi_accumulate(&tail_val, 1, MPI_INT,
                                   target_rank, gap_disp,
                                   MPI_REPLACE, &queue->win_gaps));
        }

        queue->tail++;  /* advance past this slot regardless */
    }

    return MPI_SUCCESS;
}

/**
 * @brief Single-item enqueue shim.
 *
 * Builds a BATCH_SIZE buffer filled with `value` and delegates to
 * spmc_queue_enqueue_batch.  Intended only for the demo / correctness tests;
 * the benchmark always calls enqueue_batch directly.
 */
int spmc_queue_enqueue(spmc_queue_t *queue, int value) {
    int buf[BATCH_SIZE];
    for (int i = 0; i < BATCH_SIZE; i++) buf[i] = value;
    return spmc_queue_enqueue_batch(queue, buf, BATCH_SIZE);
}

/* =========================================================================
 *  Batch Dequeue  (any consumer)
 *
 *  Algorithm — same control flow as dFFQ dequeue, but each slot contains
 *  BATCH_SIZE items read with a single mpi_get:
 *
 *    BATCH_DEQ():
 *      rank ← FAA(head, 1)           // claim one batch-slot
 *      slot ← rank mod N
 *      success ← false
 *      retry  ← 0 ; wait ← 0
 *      while ¬success AND retry < MAX_DEQUEUE_RETRIES AND wait < MAX_WAIT_COUNT:
 *        cell_rank ← AtomicRead(ranks[slot])
 *        if cell_rank == rank:        // batch is ready
 *          Get(out_data[0..k-1], datas[slot*k .. slot*k+k-1])   // bulk get
 *          AtomicWrite(ranks[slot], -1)
 *          success ← true
 *        else:
 *          gap ← AtomicRead(gaps[slot])
 *          if gap >= rank:
 *            cell_rank ← AtomicRead(ranks[slot])   // double-check
 *            if cell_rank != rank:
 *              rank ← FAA(head, 1)   // slot permanently skipped, try next
 *              slot ← rank mod N
 *              retry++ ; wait ← 0
 *            else:
 *              wait++
 *          else:
 *            wait++
 *      return out_data (BATCH_SIZE items) or empty on failure
 * ========================================================================= */

/**
 * @brief Dequeues one batch-slot (BATCH_SIZE items) into out_data[].
 * @param out_data  Caller-allocated buffer of at least BATCH_SIZE ints.
 * @param max_count Ignored; exactly BATCH_SIZE items are written on success.
 * @return BATCH_SIZE on success, 0 on empty-queue / retry exhausted.
 */
int spmc_queue_dequeue(spmc_queue_t *queue, int *out_data, int max_count) {
    if (!out_data || max_count <= 0) return 0;

    int target_rank = queue->queue_owner_rank;
    int N           = queue->size;   /* number of batch-slots */
    int k           = BATCH_SIZE;    /* items per slot */
    int no_op_val   = 0;
    int one         = 1;

    /* ---- rank ← FAA(head, 1) ---- */
    int rank;
    MPI_TRY(mpi_fetch_and_op(&one, &rank, MPI_INT,
                              target_rank, 0, MPI_SUM, &queue->win_head));

    int slot        = rank % N;
    MPI_Aint rank_disp = (MPI_Aint)slot * sizeof(int);
    MPI_Aint gap_disp  = (MPI_Aint)slot * sizeof(int);
    MPI_Aint data_disp = (MPI_Aint)slot * k * sizeof(int);

    bool success     = false;
    int  retry_count = 1;   /* we already did one FAA above */
    int  wait_count  = 0;

    while (!success &&
           retry_count < MAX_DEQUEUE_RETRIES &&
           wait_count  < MAX_WAIT_COUNT) {

        /* ---- AtomicRead(ranks[slot]) ---- */
        int cell_rank;
        MPI_TRY(mpi_fetch_and_op(&no_op_val, &cell_rank, MPI_INT,
                                  target_rank, rank_disp,
                                  MPI_NO_OP, &queue->win_ranks));

        if (cell_rank == rank) {
            /* Batch is ready: read all k items in one mpi_get. */
            MPI_TRY(mpi_get(out_data, k * sizeof(int), MPI_BYTE,
                            target_rank, data_disp, &queue->win_datas));

            /* AtomicWrite(ranks[slot], -1) — mark slot as empty. */
            int empty_val = EMPTY_CELL;
            MPI_TRY(mpi_accumulate(&empty_val, 1, MPI_INT,
                                   target_rank, rank_disp,
                                   MPI_REPLACE, &queue->win_ranks));

            success = true;

        } else {
            /* ---- AtomicRead(gaps[slot]) ---- */
            int gap;
            MPI_TRY(mpi_fetch_and_op(&no_op_val, &gap, MPI_INT,
                                      target_rank, gap_disp,
                                      MPI_NO_OP, &queue->win_gaps));

            if (gap >= rank) {
                /* Double-check rank in case the batch arrived during gap read. */
                MPI_TRY(mpi_fetch_and_op(&no_op_val, &cell_rank, MPI_INT,
                                          target_rank, rank_disp,
                                          MPI_NO_OP, &queue->win_ranks));

                if (cell_rank != rank) {
                    /* Slot permanently skipped — claim the next slot. */
                    MPI_TRY(mpi_fetch_and_op(&one, &rank, MPI_INT,
                                              target_rank, 0,
                                              MPI_SUM, &queue->win_head));
                    slot      = rank % N;
                    rank_disp = (MPI_Aint)slot * sizeof(int);
                    gap_disp  = (MPI_Aint)slot * sizeof(int);
                    data_disp = (MPI_Aint)slot * k * sizeof(int);
                    retry_count++;
                    wait_count = 0;
                } else {
                    wait_count++;
                }
            } else {
                wait_count++;
            }
        }
    }

    return success ? k : 0;
}

/* =========================================================================
 *  Utility functions
 * ========================================================================= */

void spmc_queue_print_stats(spmc_queue_t *queue) {
    if (spmc_queue_is_enqueuer(queue)) {
        printf("Queue Stats -> size: %d slots (batch=%d), head: %d, tail: %d\n",
               queue->size, BATCH_SIZE, queue->head, queue->tail);
    }
}

int spmc_queue_is_enqueuer(spmc_queue_t *queue) {
    return queue && mpi_is_root(&queue->mpi_ctx);
}

size_t spmc_queue_get_capacity_bytes(spmc_queue_t *queue) {
    if (!queue) return 0;
    if (spmc_queue_is_enqueuer(queue)) {
        /* ranks + gaps + datas + head + tail */
        return (size_t)queue->size * (2 + BATCH_SIZE) * sizeof(int)
             + 2 * sizeof(int);
    }
    return 0;
}

int spmc_queue_get_enq_batch_size(spmc_queue_t *queue) {
    (void)queue;
    return BATCH_SIZE;
}

int spmc_queue_get_deq_batch_size(spmc_queue_t *queue) {
    (void)queue;
    return BATCH_SIZE;
}

int spmc_queue_get_batch_size(spmc_queue_t *queue) {
    return spmc_queue_get_enq_batch_size(queue);
}
