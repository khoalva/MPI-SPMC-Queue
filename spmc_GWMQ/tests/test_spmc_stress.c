#define _POSIX_C_SOURCE 199309L
#include "../src/spmc_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <sys/time.h>
/**
 * Test Case 3: High throughput stress test
 * Tests performance and stability under high load
 */
void test_high_throughput(spmc_queue_t *queue) {
    int rank = mpi_get_rank(&queue->mpi_ctx);
    int size = mpi_get_size(&queue->mpi_ctx);
    
    printf("=== Test Case 3: High Throughput Stress Test ===\n");
    
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    
    if (spmc_queue_is_enqueuer(queue)) {
        printf("Rank %d: Starting high throughput production\n", rank);
        
        int num_items = 1000;  // Large number for stress testing
        int successful_enqueues = 0;
        
        for (int i = 1; i <= num_items; i++) {
            int result = spmc_queue_enqueue(queue, i);
            if (result == MPI_SUCCESS) {
                successful_enqueues++;
            } else {
                printf("WARN: Failed to enqueue item %d\n", i);
            }
            
            // No delays - maximum throughput test
            if (i % 100 == 0) {
                printf("Producer: Completed %d/%d enqueues\n", i, num_items);
            }
        }
        
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        double elapsed = (end_time.tv_sec - start_time.tv_sec) + 
                        (end_time.tv_nsec - start_time.tv_nsec) / 1e9;
        
        printf("Producer: Enqueued %d/%d items in %.3f seconds (%.1f items/sec)\n", 
               successful_enqueues, num_items, elapsed, successful_enqueues / elapsed);
        
    } else {
        printf("Rank %d: Consumer %d starting high throughput consumption\n", rank, rank - 1);
        
        int consumed_count = 0;
        int max_attempts = 2000;  // More attempts for stress test
        int consecutive_failures = 0;
        int max_consecutive_failures = 50;
        
        for (int attempt = 0; attempt < max_attempts && consecutive_failures < max_consecutive_failures; attempt++) {
            int value = spmc_queue_dequeue(queue);
            if (value != -1) {
                consumed_count++;
                consecutive_failures = 0;  // Reset failure counter
                
                if (consumed_count % 50 == 0) {
                    printf("Consumer %d: Consumed %d items\n", rank - 1, consumed_count);
                }
            } else {
                consecutive_failures++;
                usleep(1000); // 1ms delay - faster retry for stress test
            }
        }
        
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        double elapsed = (end_time.tv_sec - start_time.tv_sec) + 
                        (end_time.tv_nsec - start_time.tv_nsec) / 1e9;
        
        printf("Consumer %d: Consumed %d items in %.3f seconds (%.1f items/sec)\n", 
               rank - 1, consumed_count, elapsed, consumed_count / elapsed);
               
        if (consecutive_failures >= max_consecutive_failures) {
            printf("Consumer %d: Stopped due to %d consecutive failures\n", 
                   rank - 1, consecutive_failures);
        }
    }
}

int main(int argc, char *argv[]) {
    spmc_queue_t queue;
    
    // Initialize queue
    if (spmc_queue_init(&queue, argc, argv) != MPI_SUCCESS) {
        fprintf(stderr, "Failed to initialize SPMC queue\n");
        return -1;
    }
    
    int rank = mpi_get_rank(&queue.mpi_ctx);
    int size = mpi_get_size(&queue.mpi_ctx);
    
    printf("Starting high throughput stress test on rank %d/%d\n", rank, size);
    
    // Ensure we have at least 2 processes
    if (size < 2) {
        fprintf(stderr, "This test requires at least 2 processes\n");
        spmc_queue_destroy(&queue);
        return -1;
    }
    
    // Run test
    test_high_throughput(&queue);
    
    // Synchronize all processes
    MPI_TRY(mpi_barrier(queue.mpi_ctx.comm));
    
    if (rank == 0) {
        printf("=== High Throughput Stress Test Completed ===\n");
        spmc_queue_print_stats(&queue);
    }
    
    // Cleanup
    spmc_queue_destroy(&queue);
    
    return 0;
}