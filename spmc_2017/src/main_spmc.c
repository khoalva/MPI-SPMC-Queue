#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "mpi_lib.h"
#include "spmc_queue.h"

#define QUEUE_SIZE 1024
#define NUM_ITEMS 1000
#define PRODUCER_RANK 0

// Function prototypes
void producer_task(spmc_queue_t *queue, mpi_window_t *win, mpi_context_t *ctx);
void consumer_task(spmc_queue_t *queue, mpi_window_t *win, mpi_context_t *ctx);

int main(int argc, char *argv[]) {
    mpi_context_t ctx;
    mpi_window_t win;
    spmc_queue_t *queue;
    
    // Initialize MPI using mpi_lib
    if (mpi_init(argc, argv, &ctx) != MPI_SUCCESS) {
        fprintf(stderr, "Failed to initialize MPI\n");
        return 1;
    }
    
    printf("Process %d of %d started\n", ctx.rank, ctx.size);
    
    // Create SPMC queue
    queue = spmc_queue_create(QUEUE_SIZE, &win, &ctx);
    if (!queue) {
        fprintf(stderr, "Failed to create SPMC queue on rank %d\n", ctx.rank);
        mpi_finalize();
        return 1;
    }
    
    printf("Rank %d: SPMC queue created successfully\n", ctx.rank);
    
    // Synchronize all processes
    mpi_barrier(ctx.comm);
    
    double start_time = MPI_Wtime();
    
    // Assign roles: rank 0 is producer, others are consumers
    if (ctx.rank == PRODUCER_RANK) {
        printf("Rank %d: Starting as PRODUCER\n", ctx.rank);
        producer_task(queue, &win, &ctx);
    } else {
        printf("Rank %d: Starting as CONSUMER\n", ctx.rank);
        consumer_task(queue, &win, &ctx);
    }
    
    // Wait for all processes to complete
    mpi_barrier(ctx.comm);
    
    double end_time = MPI_Wtime();
    
    if (ctx.rank == PRODUCER_RANK) {
        printf("Total execution time: %.6f seconds\n", end_time - start_time);
    }
    
    // Cleanup
    spmc_queue_destroy(&win);
    mpi_finalize();
    
    return 0;
}

/**
 * @brief Producer task - generates and enqueues integer data
 */
void producer_task(spmc_queue_t *queue, mpi_window_t *win, mpi_context_t *ctx) {
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
            enqueued = spmc_enqueue(queue, data, win, ctx);
            
            if (!enqueued) {
                failed_attempts++;
                spmc_simulate_work(1); // Wait 1ms before retry
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
        
        // Simulate production work
        if (i % 50 == 0) {
            spmc_simulate_work(5); // 5ms work simulation
        }
    }
    
    printf("Producer: Finished. Produced %d/%d items, failed attempts: %d\n", 
           items_produced, NUM_ITEMS, failed_attempts);
}

/**
 * @brief Consumer task - dequeues and processes integer data
 */
void consumer_task(spmc_queue_t *queue, mpi_window_t *win, mpi_context_t *ctx) {
    int items_consumed = 0;
    int empty_queue_count = 0;
    bool keep_consuming = true;
    
    printf("Consumer %d: Starting to consume...\n", ctx->rank);
    
    // Consumer runs until it sees many consecutive empty queue states
    while (keep_consuming) {
        int data = 0;
        bool dequeued = spmc_dequeue(queue, ctx->rank, &data, win, ctx);
        
        if (dequeued) {
            items_consumed++;
            empty_queue_count = 0; // Reset empty count
            
            // Process the data (simulate work)
            spmc_simulate_work(2 + (ctx->rank % 3)); // Variable processing time
            
            if (items_consumed % 50 == 0) {
                printf("Consumer %d: Consumed %d items (last: %d)\n", 
                       ctx->rank, items_consumed, data);
            }
        } else {
            empty_queue_count++;
            
            // If we've seen too many empty queue states, stop consuming
            if (empty_queue_count > 1000) {
                keep_consuming = false;
            } else {
                spmc_simulate_work(1); // Wait before next attempt
            }
        }
    }
    
    printf("Consumer %d: Finished. Consumed %d items\n", ctx->rank, items_consumed);
}
