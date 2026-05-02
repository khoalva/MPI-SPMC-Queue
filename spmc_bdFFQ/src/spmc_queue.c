#define _POSIX_C_SOURCE 199309L
#define _GNU_SOURCE  // Add this for broader usleep support

#include "spmc_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/* ========================================================================
 *  Batch dFFQ — MPI RMA implementation using mpi_lib wrappers
 *
 *  Pseudocode reference: Algorithms 9-12 (Batch dFFQ Init / Enqueue /
 *  Dequeue with SCAN / RESOLVE / COMMIT).
 *
 *  MPI operation mapping (from the user-provided table):
 *    Read   → mpi_get_accumulate  (MPI_NO_OP)   — atomic read
 *    Write  → mpi_accumulate      (MPI_REPLACE)  — atomic write
 *    Put    → mpi_put                            — non-atomic write
 *    Get    → mpi_get                            — non-atomic read
 *    FAA    → mpi_fetch_and_op    (MPI_SUM)      — fetch-and-add
 *    Swap   → mpi_fetch_and_op    (MPI_REPLACE)  — atomic swap
 *    Or     → mpi_accumulate      (MPI_BOR)      — atomic bitwise OR
 * ======================================================================== */

/* -----------------------------------------------------------------------
 * Helper: Atomically Read k contiguous elements starting at ring position
 *         `base` from the given window.
 *         Uses mpi_get_accumulate with MPI_NO_OP (atomic bulk read).
 * ----------------------------------------------------------------------- */
static int read_contiguous(int *result_buf, int k, int base,
                           int target_rank, int queue_size,
                           mpi_window_t *win) {
    int pos = base % queue_size;
    MPI_Aint disp = (MPI_Aint)pos * sizeof(int);

    /* Scratch origin buffer required by mpi_get_accumulate (unused for NO_OP) */
    int zero = 0;

    return mpi_get_accumulate(
        &zero, k, MPI_INT,          /* origin (ignored for NO_OP) */
        result_buf, k, MPI_INT,     /* result */
        target_rank, disp,          /* target displacement */
        k, MPI_INT,                 /* target count + type */
        MPI_NO_OP, win);
}

/* -----------------------------------------------------------------------
 * Helper: Atomically Write values[] to selected positions indicated by
 *         the boolean mask `selected[]`.  Positions are relative to `base`
 *         inside the ring buffer.
 *         Uses mpi_accumulate with MPI_REPLACE.
 * ----------------------------------------------------------------------- */
static void write_selected(int *values, bool *selected, int k,
                           int base, int target_rank, int queue_size,
                           mpi_window_t *win) {
    /* Coalesce contiguous selected runs into single MPI_Accumulate calls. */
    int i = 0;
    while (i < k) {
        while (i < k && !selected[i]) i++;
        if (i >= k) break;

        int run_start = i;
        int run_len = 0;
        while (i < k && selected[i]) { run_len++; i++; }

        int pos = (base + run_start) % queue_size;
        MPI_Aint disp = (MPI_Aint)pos * sizeof(int);
        mpi_accumulate(&values[run_start], run_len, MPI_INT,
                       target_rank, disp, MPI_REPLACE, win);
    }
}

/* -----------------------------------------------------------------------
 * Helper: Non-atomically Put data[] to selected positions indicated by
 *         the boolean mask `selected[]`.
 *         Uses mpi_put.
 * ----------------------------------------------------------------------- */
static void put_selected(int *data, int data_offset, bool *selected, int k,
                         int base, int target_rank, int queue_size,
                         mpi_window_t *win) {
    int i = 0;
    while (i < k) {
        while (i < k && !selected[i]) i++;
        if (i >= k) break;

        int run_start = i;
        int run_len = 0;
        while (i < k && selected[i]) { run_len++; i++; }

        int pos = (base + run_start) % queue_size;
        MPI_Aint disp = (MPI_Aint)pos * sizeof(int);
        mpi_put(&data[data_offset + run_start], run_len, MPI_INT,
                target_rank, disp, win);
    }
}

/* -----------------------------------------------------------------------
 * Helper: Non-atomically Get contiguous elements from ring buffer.
 *         Uses mpi_get.
 * ----------------------------------------------------------------------- */
static int get_contiguous(int *result_buf, int k, int base,
                          int target_rank, int queue_size,
                          mpi_window_t *win) {
    int pos = base % queue_size;
    MPI_Aint disp = (MPI_Aint)pos * sizeof(int);
    return mpi_get(result_buf, k, MPI_INT, target_rank, disp, win);
}

/* -----------------------------------------------------------------------
 * Helper: Atomically read selected positions from `win` into `result_buf`.
 *         Only positions where selected[j] == true are read.
 *         Coalesces contiguous selected runs into single bulk calls.
 *         Uses mpi_get_accumulate with MPI_NO_OP.
 * ----------------------------------------------------------------------- */
static void read_selected(int *result_buf, bool *selected, int k,
                          int base, int target_rank, int queue_size,
                          mpi_window_t *win) {
    int zero = 0;
    int i = 0;
    while (i < k) {
        while (i < k && !selected[i]) i++;
        if (i >= k) break;

        int run_start = i;
        int run_len = 0;
        while (i < k && selected[i]) { run_len++; i++; }

        int pos = (base + run_start) % queue_size;
        MPI_Aint disp = (MPI_Aint)pos * sizeof(int);
        mpi_get_accumulate(&zero, run_len, MPI_INT,
                           &result_buf[run_start], run_len, MPI_INT,
                           target_rank, disp, run_len, MPI_INT,
                           MPI_NO_OP, win);
    }
}

/* ========================================================================
 *  Initialization / Destruction  (mirrors c-FFQ exactly)
 * ======================================================================== */

/**
 * @brief Initializes the SPMC queue and necessary MPI resources with configurable queue owner.
 */
int spmc_queue_init_with_queue_owner(spmc_queue_t *queue, int argc, char *argv[], int queue_owner_rank) {
    if (!queue) return -1;

    MPI_TRY(mpi_init(argc, argv, &queue->mpi_ctx));

    if (mpi_get_size(&queue->mpi_ctx) < 2) {
        mpi_finalize();
        return -1;
    }

    int rank = mpi_get_rank(&queue->mpi_ctx);
    int size = mpi_get_size(&queue->mpi_ctx);

    /* Validate queue owner rank */
    if (queue_owner_rank < 0 || queue_owner_rank >= size) {
        if (rank == 0) {
            fprintf(stderr, "Invalid queue owner rank %d (must be 0-%d)\n", queue_owner_rank, size - 1);
        }
        mpi_finalize();
        return -1;
    }

    queue->queue_owner_rank = queue_owner_rank;
    queue->size = MAX_QUEUE_SIZE;

    int is_queue_owner = (rank == queue_owner_rank);

    /* Only the queue owner allocates and initializes queue memory. */
    if (is_queue_owner) {
        queue->ranks = malloc(queue->size * sizeof(int));
        queue->gaps  = malloc(queue->size * sizeof(int));
        queue->datas = malloc(queue->size * sizeof(int));
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
            queue->ranks[i] = EMPTY_CELL;  /* -1 means empty/available */
            queue->gaps[i]  = 0;
            queue->datas[i] = 0;
        }
    } else {
        queue->ranks = NULL;
        queue->gaps  = NULL;
        queue->datas = NULL;
        queue->head  = 0;
        queue->tail  = 0;
    }

    /* Create MPI windows for one-sided access. Size is 0 for non-queue-owner. */
    size_t array_size = is_queue_owner ? queue->size * sizeof(int) : 0;
    size_t head_size  = is_queue_owner ? sizeof(int) : 0;
    MPI_TRY(mpi_win_create(queue->ranks, array_size, sizeof(int), queue->mpi_ctx.comm, &queue->win_ranks));
    MPI_TRY(mpi_win_create(queue->gaps,  array_size, sizeof(int), queue->mpi_ctx.comm, &queue->win_gaps));
    MPI_TRY(mpi_win_create(queue->datas, array_size, sizeof(int), queue->mpi_ctx.comm, &queue->win_datas));
    MPI_TRY(mpi_win_create(&queue->head, head_size,  sizeof(int), queue->mpi_ctx.comm, &queue->win_head));

    /* Lock all windows for passive target synchronization. */
    mpi_window_t windows[] = {queue->win_ranks, queue->win_gaps, queue->win_datas, queue->win_head};
    MPI_TRY(mpi_win_lock_all_multiple(windows, 4));

    printf("bdFFQ SPMC Queue initialized on rank %d/%d (queue_owner=%d)\n",
           rank, size, queue_owner_rank);

    return MPI_SUCCESS;
}

/* Backward compatibility: default queue owner at rank 0. */
int spmc_queue_init(spmc_queue_t *queue, int argc, char *argv[]) {
    return spmc_queue_init_with_queue_owner(queue, argc, argv, 0);
}

/**
 * @brief Destroys the queue and frees all associated resources.
 */
void spmc_queue_destroy(spmc_queue_t *queue) {
    if (!queue) return;

    mpi_window_t windows[] = {queue->win_ranks, queue->win_gaps, queue->win_datas, queue->win_head};
    mpi_win_unlock_all_multiple(windows, 4);

    mpi_win_destroy(&queue->win_ranks);
    mpi_win_destroy(&queue->win_gaps);
    mpi_win_destroy(&queue->win_datas);
    mpi_win_destroy(&queue->win_head);

    /* The queue owner frees the memory it allocated. */
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

/* ========================================================================
 *  Algorithm 10 — Batch Enqueue
 *
 *  BATCH_ENQ(data[], k):
 *    success ← false ; i ← 0
 *    while ¬success:
 *      ranks ← Read(ranks, tail mod N, k − i)        // atomic bulk read
 *      (ready, busy) ← CLASSIFY(ranks, tail)
 *      Write(gaps, tail mod N, busy)                  // mark gaps
 *      Put(datas, tail mod N, ready, data, i)         // write data
 *      Write(ranks, tail mod N, ready)                // publish ranks
 *      tail ← tail + k − i
 *      i ← i + |ready|
 *      if i == k: success ← true
 * ======================================================================== */

int spmc_queue_enqueue_batch(spmc_queue_t *queue, int *values, int count) {
    if (!spmc_queue_is_enqueuer(queue)) return -1;
    if (!values || count <= 0) return -1;

    int target_rank = queue->queue_owner_rank;
    int k = count;
    int N = queue->size;

    /* Temporary buffers — worst case is `k` elements */
    int  *ranks_buf   = malloc(k * sizeof(int));
    bool *ready_mask  = malloc(k * sizeof(bool));
    bool *busy_mask   = malloc(k * sizeof(bool));
    int  *tail_vals   = malloc(k * sizeof(int));  /* values for Write(ranks) and Write(gaps) */
    if (!ranks_buf || !ready_mask || !busy_mask || !tail_vals) {
        free(ranks_buf); free(ready_mask); free(busy_mask); free(tail_vals);
        return -1;
    }

    bool success = false;
    int  i = 0;  /* number of items successfully enqueued so far */

    while (!success) {
        int remaining = k - i;
        int tail = queue->tail;

        /* ---- Step 1: Atomic Read of ranks[tail..tail+remaining-1] ---- */
        read_contiguous(ranks_buf, remaining, tail, target_rank, N, &queue->win_ranks);

        /* ---- CLASSIFY: separate ready (rank < 0) vs busy (rank >= 0) ---- */
        int ready_count = 0;
        for (int j = 0; j < remaining; j++) {
            if (ranks_buf[j] < 0) {
                ready_mask[j] = true;
                busy_mask[j]  = false;
                ready_count++;
            } else {
                ready_mask[j] = false;
                busy_mask[j]  = true;
            }
        }

        /* ---- Step 2: Write(gaps, tail, busy) — mark gaps for busy cells ---- */
        if (ready_count < remaining) {
            /* For busy cells, write tail value into gaps to signal the gap */
            for (int j = 0; j < remaining; j++) {
                tail_vals[j] = tail + j;
            }
            write_selected(tail_vals, busy_mask, remaining,
                           tail, target_rank, N, &queue->win_gaps);
        }

        /* ---- Step 3: Put(datas, tail, ready, data, i) ---- */
        if (ready_count > 0) {
            put_selected(values, i, ready_mask, remaining,
                         tail, target_rank, N, &queue->win_datas);

            /* ---- Step 4: Write(ranks, tail, ready) — publish ---- */
            for (int j = 0; j < remaining; j++) {
                tail_vals[j] = tail + j;
            }
            write_selected(tail_vals, ready_mask, remaining,
                           tail, target_rank, N, &queue->win_ranks);
        }

        /* ---- Step 5: advance tail and accumulate progress ---- */
        queue->tail = tail + remaining;
        i += ready_count;

        if (i == k) {
            success = true;
        }
    }

    free(ranks_buf);
    free(ready_mask);
    free(busy_mask);
    free(tail_vals);

    return 0;
}

/**
 * @brief Single-item enqueue — wraps batch enqueue with k=1.
 */
int spmc_queue_enqueue(spmc_queue_t *queue, int value) {
    return spmc_queue_enqueue_batch(queue, &value, 1);
}

/* ========================================================================
 *  Algorithm 12 — Batch Dequeue
 *
 *  BATCH_DEQ(k):
 *    base ← FAA(head, k) mod N
 *    loop:
 *      (Ready, Pending) ← SCAN(base, k)
 *      (extra, needWait) ← RESOLVE(base, Pending)
 *      Combined ← Ready ∪ extra
 *      if needWait:           WAIT ; continue
 *      else if Combined ≠ ∅:  return COMMIT(base, Combined)
 *      else:                  base ← FAA(head, k)   // whole range skipped
 *
 *  SCAN(base, k):
 *    rSnap ← Read(ranks[base : base+k])
 *    Ready   = { i | rSnap[i] == base+i }
 *    Pending = { i | rSnap[i] <  base+i }
 *
 *  RESOLVE(base, Pending):
 *    if Pending == ∅: return (∅, false)
 *    gSnap ← Read(gaps[base : base+k])
 *    Candidates = { i ∈ Pending | gSnap[i] >= base+i }
 *    needWait   = ∃ i ∈ Pending : gSnap[i] < base+i
 *    extra = ∅
 *    if Candidates ≠ ∅:
 *      rCheck ← Read(ranks, base, Candidates)   // double-check
 *      extra  = { i ∈ Candidates | rCheck[i] == base+i }
 *    return (extra, needWait)
 *
 *  COMMIT(base, ReadySet):
 *    Data ← Get(datas, base, ReadySet)
 *    Write(ranks, base, ReadySet, −1)   // mark dequeued
 *    return Data
 * ======================================================================== */

int spmc_queue_dequeue(spmc_queue_t *queue, int *out_data, int max_count) {
    if (!out_data || max_count <= 0) return 0;

    int target_rank = queue->queue_owner_rank;
    int k = max_count;
    int N = queue->size;

    /* Allocate working buffers */
    int  *ranks_buf  = malloc(k * sizeof(int));
    int  *gaps_buf   = malloc(k * sizeof(int));
    int  *datas_buf  = malloc(k * sizeof(int));
    bool *ready_set  = malloc(k * sizeof(bool));
    bool *pending    = malloc(k * sizeof(bool));
    bool *candidates = malloc(k * sizeof(bool));
    bool *combined   = malloc(k * sizeof(bool));
    int  *empty_vals = malloc(k * sizeof(int));
    if (!ranks_buf || !gaps_buf || !datas_buf || !ready_set ||
        !pending || !candidates || !combined || !empty_vals) {
        free(ranks_buf); free(gaps_buf); free(datas_buf);
        free(ready_set); free(pending); free(candidates);
        free(combined); free(empty_vals);
        return 0;
    }

    /* Prepare EMPTY_CELL values for COMMIT */
    for (int j = 0; j < k; j++) empty_vals[j] = EMPTY_CELL;

    int retry_count = 0;

    /* Line 2: base ← FAA(head, k) */
    int base;
    MPI_TRY(mpi_fetch_and_op(&k, &base, MPI_INT, target_rank, 0, MPI_SUM, &queue->win_head));

    /* Line 3: while true do */
    while (retry_count < MAX_DEQUEUE_RETRIES) {
        retry_count++;

        /* ============================================================
         *  SCAN(base, k)  — Algorithm 12, lines 16-19
         * ============================================================ */
        /* rSnap ← Read(ranks[base : base+k]) */
        read_contiguous(ranks_buf, k, base, target_rank, N, &queue->win_ranks);

        bool has_pending = false;
        for (int j = 0; j < k; j++) {
            if (ranks_buf[j] == base + j) {
                ready_set[j] = true;
                pending[j]   = false;
            } else if (ranks_buf[j] < base + j) {
                ready_set[j] = false;
                pending[j]   = true;
                has_pending  = true;
            } else {
                /* ranks_buf[j] > base+j — should not happen in correct usage;
                   treat as not ready, not pending (skip). */
                ready_set[j] = false;
                pending[j]   = false;
            }
        }

        /* ============================================================
         *  RESOLVE(base, Pending)  — Algorithm 12, lines 20-32
         * ============================================================ */
        bool needWait = false;
        for (int j = 0; j < k; j++) candidates[j] = false;

        if (has_pending) {
            /* gSnap ← Read(gaps[base : base+k]) */
            read_contiguous(gaps_buf, k, base, target_rank, N, &queue->win_gaps);

            bool has_candidates = false;
            for (int j = 0; j < k; j++) {
                if (!pending[j]) continue;
                if (gaps_buf[j] >= base + j) {
                    candidates[j]  = true;
                    has_candidates = true;
                } else {
                    /* gSnap[j] < base + j → producer hasn't written gap yet */
                    needWait = true;
                }
            }

            if (has_candidates) {
                /* rCheck ← Read(ranks, base, Candidates)  — double-check */
                read_selected(ranks_buf, candidates, k,
                              base, target_rank, N, &queue->win_ranks);

                for (int j = 0; j < k; j++) {
                    if (candidates[j] && ranks_buf[j] == base + j) {
                        /* The cell was filled between SCAN and now → extra.
                           candidates[j] remains true → merged into combined. */
                    } else if (candidates[j]) {
                        /* Confirmed skipped — clear candidate flag */
                        candidates[j] = false;
                    }
                }
            }
        }

        /* ============================================================
         *  Combined ← Ready ∪ extra
         * ============================================================ */
        bool has_combined = false;
        for (int j = 0; j < k; j++) {
            combined[j] = ready_set[j] || candidates[j];
            if (combined[j]) has_combined = true;
        }

        /* ============================================================
         *  Decision
         * ============================================================ */
        if (needWait && !has_combined) {
            /* Line 8: WAIT — maintain range integrity */
            usleep(10);
            continue;
        } else if (has_combined) {
            /* ========================================================
             *  COMMIT(base, Combined)  — Algorithm 12, lines 33-37
             * ======================================================== */

            /* Data ← Get(datas, base, Combined) — vectorized gather */
            /* Read the full contiguous block and pick combined entries */
            get_contiguous(datas_buf, k, base, target_rank, N, &queue->win_datas);

            /* Write(ranks, base, Combined, -1) — vectorized scatter */
            write_selected(empty_vals, combined, k,
                           base, target_rank, N, &queue->win_ranks);

            /* Copy combined data to output, maintaining order */
            int out_idx = 0;
            for (int j = 0; j < k; j++) {
                if (combined[j]) {
                    out_data[out_idx++] = datas_buf[j];
                }
            }

            free(ranks_buf); free(gaps_buf); free(datas_buf);
            free(ready_set); free(pending); free(candidates);
            free(combined); free(empty_vals);
            return out_idx;
        } else {
            /* Line 12: entire range was skipped → base ← FAA(head, k) */
            MPI_TRY(mpi_fetch_and_op(&k, &base, MPI_INT, target_rank, 0, MPI_SUM, &queue->win_head));
        }
    }

    /* Retry limit reached — return 0 items */
    free(ranks_buf); free(gaps_buf); free(datas_buf);
    free(ready_set); free(pending); free(candidates);
    free(combined); free(empty_vals);
    return 0;
}

/* ========================================================================
 *  Utility functions  (same interface as c-FFQ)
 * ======================================================================== */

/**
 * @brief Prints statistics about the queue.
 */
void spmc_queue_print_stats(spmc_queue_t *queue) {
    if (spmc_queue_is_enqueuer(queue)) {
        printf("Queue Stats -> size: %d, head: %d, tail: %d\n", queue->size, queue->head, queue->tail);
    }
}

/**
 * @brief Checks if the current process is the producer (rank 0).
 */
int spmc_queue_is_enqueuer(spmc_queue_t *queue) {
    return queue && mpi_is_root(&queue->mpi_ctx);
}

/**
 * @brief Returns the total bytes allocated by the queue.
 */
size_t spmc_queue_get_capacity_bytes(spmc_queue_t *queue) {
    if (!queue) return 0;
    if (spmc_queue_is_enqueuer(queue)) {
        return 3 * queue->size * sizeof(int) + sizeof(queue->head) + sizeof(queue->tail);
    }
    return 0;
}

/**
 * @brief Separate batch-size queries for producer and consumer sides.
 */
int spmc_queue_get_enq_batch_size(spmc_queue_t *queue) {
    (void)queue;
    return BATCH_SIZE;  /* bdFFQ enqueue batch size */
}

int spmc_queue_get_deq_batch_size(spmc_queue_t *queue) {
    (void)queue;
    return BATCH_SIZE;  /* bdFFQ dequeue batch size */
}

/* Legacy alias */
int spmc_queue_get_batch_size(spmc_queue_t *queue) {
    return spmc_queue_get_enq_batch_size(queue);
}
