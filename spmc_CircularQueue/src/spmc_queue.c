#define _POSIX_C_SOURCE 199309L
#define _GNU_SOURCE
#include "spmc_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

// Backoff constants
#define BACKOFF_MIN_US 1
#define BACKOFF_MAX_US 1000

static void backoff(int *backoff_time) {
    usleep(*backoff_time);
    *backoff_time = (*backoff_time * 2 < BACKOFF_MAX_US) ? *backoff_time * 2 : BACKOFF_MAX_US;
}

/**
 * @brief Initializes the Circular Queue and necessary MPI resources.
 */
int spmc_queue_init(spmc_queue_t *queue, int argc, char *argv[]) {
    if (!queue) return -1;

    MPI_TRY(mpi_init(argc, argv, &queue->mpi_ctx));

    if (mpi_get_size(&queue->mpi_ctx) < 2) {
        mpi_finalize();
        return -1;
    }

    queue->size = MAX_QUEUE_SIZE;
    int is_producer = (mpi_get_rank(&queue->mpi_ctx) == 0);

    // Only the producer (rank 0) allocates and initializes the queue memory.
    if (is_producer) {
        queue->data = malloc(queue->size * sizeof(int));
        if (!queue->data) {
            mpi_finalize();
            return -1;
        }
        
        // Initialize counters
        queue->head = 0;
        queue->tail = 0;
        queue->reserved_head = 0;
        queue->reserved_tail = 0;
        queue->head_buf = 0;
        queue->tail_buf = 0;
        
        // Initialize data array
        for (int i = 0; i < queue->size; i++) {
            queue->data[i] = 0;
        }
    } else {
        // Consumers do not allocate the main memory.
        queue->data = NULL;
        queue->head = 0;
        queue->tail = 0;
        queue->reserved_head = 0;
        queue->reserved_tail = 0;
        queue->head_buf = 0;
        queue->tail_buf = 0;
    }

    // Create MPI windows for one-sided access. The size is 0 for consumers.
    size_t data_size = is_producer ? queue->size * sizeof(int) : 0;
    size_t counter_size = is_producer ? sizeof(int) : 0;
    
    MPI_TRY(mpi_win_create(queue->data, data_size, sizeof(int), queue->mpi_ctx.comm, &queue->win_data));
    MPI_TRY(mpi_win_create(&queue->tail, counter_size, sizeof(int), queue->mpi_ctx.comm, &queue->win_tail));
    MPI_TRY(mpi_win_create(&queue->head, counter_size, sizeof(int), queue->mpi_ctx.comm, &queue->win_head));
    MPI_TRY(mpi_win_create(&queue->reserved_head, counter_size, sizeof(int), queue->mpi_ctx.comm, &queue->win_reserved_head));
    MPI_TRY(mpi_win_create(&queue->reserved_tail, counter_size, sizeof(int), queue->mpi_ctx.comm, &queue->win_reserved_tail));

    // Lock all windows to enable passive target synchronization
    mpi_window_t windows[] = {queue->win_data, queue->win_tail, queue->win_head, 
                              queue->win_reserved_head, queue->win_reserved_tail};
    MPI_TRY(mpi_win_lock_all_multiple(windows, 5));

    printf("Circular Queue initialized on rank %d/%d\n",
           mpi_get_rank(&queue->mpi_ctx), mpi_get_size(&queue->mpi_ctx));

    return MPI_SUCCESS;
}

/**
 * @brief Destroys the queue and frees all associated resources.
 */
void spmc_queue_destroy(spmc_queue_t *queue) {
    if (!queue) return;

    mpi_window_t windows[] = {queue->win_data, queue->win_tail, queue->win_head,
                              queue->win_reserved_head, queue->win_reserved_tail};
    mpi_win_unlock_all_multiple(windows, 5);

    mpi_win_destroy(&queue->win_data);
    mpi_win_destroy(&queue->win_tail);
    mpi_win_destroy(&queue->win_head);
    mpi_win_destroy(&queue->win_reserved_head);
    mpi_win_destroy(&queue->win_reserved_tail);

    // The producer frees the memory it allocated.
    if (spmc_queue_is_enqueuer(queue) && queue->data) {
        free(queue->data);
        queue->data = NULL;
    }
    
    mpi_finalize();
}

/**
 * @brief Enqueues an item using Circular Queue logic. Only the producer (rank 0) should call this.
 * @return MPI_SUCCESS on success, or -1 if the queue is full.
 */
int spmc_queue_enqueue(spmc_queue_t *queue, int value) {
    if (!spmc_queue_is_enqueuer(queue)) return -1;
    
    int backoff_time = BACKOFF_MIN_US;
    
    // Fetch and increment tail atomically
    int old_tail;
    int one = 1;
    MPI_TRY(mpi_fetch_and_op(&one, &old_tail, MPI_INT, 0, 0, MPI_SUM, &queue->win_tail));
    
    int new_tail = old_tail + 1;
    
    // Check if queue has space: (new_tail - head_buf) > capacity
    if (new_tail - queue->head_buf > queue->size) {
        // Update head_buf by reading reserved_head
        int no_op = 0;
        MPI_TRY(mpi_fetch_and_op(&no_op, &queue->head_buf, MPI_INT, 0, 0, MPI_NO_OP, &queue->win_reserved_head));
        
        // Check again after update
        if (new_tail - queue->head_buf > queue->size) {
            // Queue is full, rollback tail
            int minus_one = -1;
            MPI_TRY(mpi_fetch_and_op(&minus_one, &old_tail, MPI_INT, 0, 0, MPI_SUM, &queue->win_tail));
            return -1;
        }
    }
    
    // Calculate position in circular buffer
    int pos = old_tail % queue->size;
    
    // Write data to circular buffer
    MPI_Aint data_disp = pos * sizeof(int);
    MPI_TRY(mpi_put(&value, sizeof(int), MPI_BYTE, 0, data_disp, &queue->win_data));
    
    // Flush to ensure data is written
    MPI_TRY(MPI_Win_flush(0, queue->win_data.window));
    
    // Update reserved_tail using compare-and-swap in a loop until successful
    int expected = old_tail;
    int desired = new_tail;
    int result;
    
    do {
        MPI_TRY(mpi_compare_and_swap(&desired, &expected, &result, MPI_INT, 0, 0, &queue->win_reserved_tail));
        
        if (result != expected) {
            // CAS failed, backoff and retry
            backoff(&backoff_time);
            expected = old_tail; // Reset expected value
        }
    } while (result != old_tail);
    
    return MPI_SUCCESS;
}

/**
 * @brief Dequeues items using Circular Queue logic.
 * @param queue The queue structure
 * @param out_data Array to store dequeued values
 * @param max_count Maximum number of items to dequeue
 * @return Number of items dequeued (0 to max_count)
 */
int spmc_queue_dequeue(spmc_queue_t *queue, int *out_data, int max_count) {
    if (!queue || !out_data || max_count <= 0) return 0;

    int count = max_count;          // keep general; user says max_count is set to 1 in benchmark
    int backoff_time = BACKOFF_MIN_US;

    // 1) Reserve 'count' slots by atomically advancing head
    int old_head = 0;
    MPI_TRY(mpi_fetch_and_op(&count, &old_head, MPI_INT, 0, 0, MPI_SUM, &queue->win_head));
    int new_head = old_head + count;

    // 2) Ensure producers have committed at least 'count' items:
    //    We must not read beyond reserved_tail.
    if (new_head > queue->tail_buf) {
        int no_op = 0;
        MPI_TRY(mpi_fetch_and_op(&no_op, &queue->tail_buf, MPI_INT,
                                 0, 0, MPI_NO_OP, &queue->win_reserved_tail));

        if (new_head > queue->tail_buf) {
            // Not enough committed items; rollback head reservation and fail.
            int rollback = -count;
            int ignored = 0;
            MPI_TRY(mpi_fetch_and_op(&rollback, &ignored, MPI_INT, 0, 0, MPI_SUM, &queue->win_head));
            return 0;
        }
    }

    // 3) Read data from the circular buffer (handle wrap-around)
    int pos = old_head % queue->size;

    if (pos + count <= queue->size) {
        MPI_Aint data_disp = (MPI_Aint)pos * (MPI_Aint)sizeof(int);
        MPI_TRY(mpi_get(out_data, count * (int)sizeof(int), MPI_BYTE,
                        0, data_disp, &queue->win_data));
    } else {
        int first_get_nelem = queue->size - pos;

        MPI_Aint disp1 = (MPI_Aint)pos * (MPI_Aint)sizeof(int);
        MPI_TRY(mpi_get(out_data, first_get_nelem * (int)sizeof(int), MPI_BYTE,
                        0, disp1, &queue->win_data));

        MPI_Aint disp2 = 0;
        MPI_TRY(mpi_get(out_data + first_get_nelem,
                        (count - first_get_nelem) * (int)sizeof(int), MPI_BYTE,
                        0, disp2, &queue->win_data));
    }

    // Ensure the GETs are locally complete/visible before we "commit" the pop.
    MPI_TRY(MPI_Win_flush(0, queue->win_data.window));

    // 4) Commit pop in-order: reserved_head must advance from old_head -> new_head
    //    This may spin until earlier pops commit.
    int expected = old_head;
    int desired  = new_head;
    int result   = 0;

    for (;;) {
        MPI_TRY(mpi_compare_and_swap(&desired, &expected, &result, MPI_INT,
                                     0, 0, &queue->win_reserved_head));

        if (result == expected) {
            // CAS succeeded; pop is committed
            break;
        }

        backoff(&backoff_time);
        // expected stays old_head: we are waiting for reserved_head to reach our old_head.
    }

    return count;
}

/**
 * @brief Prints statistics about the queue.
 */
void spmc_queue_print_stats(spmc_queue_t *queue) {
    if (spmc_queue_is_enqueuer(queue)) {
        printf("Circular Queue Stats -> size: %d, head: %d, tail: %d, reserved_head: %d, reserved_tail: %d\n", 
               queue->size, queue->head, queue->tail, queue->reserved_head, queue->reserved_tail);
    }
}

/**
 * @brief Checks if the current process is the producer (rank 0).
 */
int spmc_queue_is_enqueuer(spmc_queue_t *queue) {
    return queue && mpi_is_root(&queue->mpi_ctx);
}

/**
 * @brief Returns the batch size for dequeue operations.
 */
int spmc_queue_get_batch_size(spmc_queue_t *queue) {
    // Circular Queue supports batch dequeue
    return 1; // Can be adjusted based on workload
}

/**
 * @brief Returns the total bytes allocated by the queue.
 */
size_t spmc_queue_get_capacity_bytes(spmc_queue_t *queue) {
    if (!queue) return 0;
    if (spmc_queue_is_enqueuer(queue)) {
        return queue->size * sizeof(int) + 
               sizeof(queue->head) + sizeof(queue->tail) +
               sizeof(queue->reserved_head) + sizeof(queue->reserved_tail) +
               sizeof(queue->head_buf) + sizeof(queue->tail_buf);
    }
    return 0;
}