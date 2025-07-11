#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "mpi_lib.h"
#include "spmc_queue.h"

#define QUEUE_SIZE 64  // Smaller queue to force more interaction
#define NUM_ITEMS 1000
#define PRODUCER_RANK 0

// Global flag to signal when producer is done
volatile int producer_finished = 0;

// Function prototypes
void producer_task(spmc_queue_t *queue, mpi_window_t *win);
void consumer_task(spmc_queue_t *queue, mpi_window_t *win);

int main(int argc, char *argv[]) {
    mpi_context_t ctx;
    mpi_window_t win;
    // Cấp phát queue với kích thước tối đa (hoặc phù hợp)
    spmc_queue_t *queue = malloc(sizeof(spmc_queue_t) + QUEUE_SIZE * sizeof(spmc_cell_t));
    if (!queue) {
        fprintf(stderr, "Failed to allocate queue memory\n");
        return 1;
    }

    // Khởi tạo MPI
    if (mpi_init(argc, argv, &ctx) != MPI_SUCCESS) {
        fprintf(stderr, "Failed to initialize MPI\n");
        free(queue);
        return 1;
    }

    printf("Process %d of %d started\n", ctx.rank, ctx.size);

    // Gán context vào queue trước khi init
    queue->mpi_ctx = ctx;

    // Khởi tạo queue (không dùng global_ctx)
    if (spmc_queue_init(queue, argc, argv) != 0) {
        fprintf(stderr, "Failed to initialize SPMC queue on rank %d\n", ctx.rank);
        mpi_finalize();
        free(queue);
        return 1;
    }

    printf("Rank %d: SPMC queue initialized successfully\n", ctx.rank);

    mpi_barrier(ctx.comm);

    double start_time = MPI_Wtime();

    if (ctx.rank == PRODUCER_RANK) {
        printf("Rank %d: Starting as PRODUCER\n", ctx.rank);
        producer_task(queue, &win);
    } else {
        printf("Rank %d: Starting as CONSUMER\n", ctx.rank);
        consumer_task(queue, &win);
    }

    mpi_barrier(ctx.comm);

    double end_time = MPI_Wtime();

    if (ctx.rank == PRODUCER_RANK) {
        printf("Total execution time: %.6f seconds\n", end_time - start_time);
    }

    mpi_win_destroy(&win);
    mpi_finalize();
    free(queue);

    return 0;
}

/**
 * @brief Producer task - generates and enqueues integer data with controlled rate
 */
void producer_task(spmc_queue_t *queue, mpi_window_t *win) {
    int items_produced = 0;
    int failed_attempts = 0;

    printf("Producer: Starting to produce %d items...\n", NUM_ITEMS);
    for (int i = 0; i < NUM_ITEMS; i++) {
        int data = i + 1000; // Example: just use i+1000 as data
        
        // Try to enqueue with retry logic
        bool enqueued = false;
        int retry_count = 0;
        const int max_retries = 100;
        
        while (!enqueued && retry_count < max_retries) {
            enqueued = spmc_enqueue(queue, data, win);
            
            if (!enqueued) {
                failed_attempts++;
                spmc_simulate_work(5); // Wait longer when queue is full
                retry_count++;
            }
        }
        
        if (enqueued) {
            items_produced++;
            if (items_produced % 100 == 0) {
                printf("Producer: Produced %d items (failed attempts: %d)\n", 
                       items_produced, failed_attempts);
            }
        } else {
            printf("Producer: Failed to enqueue item %d after %d retries\n", i, max_retries);
        }
        
        // Add consistent delay to slow down producer and allow consumers to keep up
        spmc_simulate_work(2); // 2ms delay after each item
        
        // Additional delay every 10 items to give consumers more time
        if (i % 10 == 0) {
            spmc_simulate_work(5); // Extra 5ms delay
        }
    }
    
    // Signal that producer is finished by setting a flag in shared memory
    // This approach uses the queue's shared memory to communicate completion
    producer_finished = 1;
    
    printf("Producer: Finished. Produced %d/%d items, failed attempts: %d\n", 
           items_produced, NUM_ITEMS, failed_attempts);
}

/**
 * @brief Consumer task - dequeues and processes integer data with immediate start
 */
void consumer_task(spmc_queue_t *queue, mpi_window_t *win) {
    int items_consumed = 0;
    int consecutive_empty_attempts = 0;
    bool keep_consuming = true;
    mpi_context_t *ctx = &queue->mpi_ctx;
    printf("Consumer %d: Starting to consume...\n", ctx->rank);
    while (keep_consuming) {
        int data = 0;
        bool dequeued = spmc_dequeue(queue, ctx->rank, &data, win);
        
        if (dequeued) {
            items_consumed++;
            consecutive_empty_attempts = 0; // Reset empty count
            
            // Process the data with minimal delay to keep up with producer
            spmc_simulate_work(1); // Minimal processing time
            
            // More frequent progress reporting to show real-time consumption
            if (items_consumed % 25 == 0) {
                printf("Consumer %d: Consumed %d items (last: %d)\n", 
                       ctx->rank, items_consumed, data);
            }
        } else {
            consecutive_empty_attempts++;
            
            // Be more patient - allow for producer to catch up
            if (consecutive_empty_attempts > 500) {
                keep_consuming = false;
                printf("Consumer %d: Stopping after %d consecutive empty attempts\n", 
                       ctx->rank, consecutive_empty_attempts);
            } else {
                // Very short wait when queue is empty to check frequently
                spmc_simulate_work(1); // Just 1ms wait
            }
        }
    }
    
    printf("Consumer %d: Finished. Consumed %d items\n", ctx->rank, items_consumed);
}