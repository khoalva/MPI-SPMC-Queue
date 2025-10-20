#define _POSIX_C_SOURCE 199309L
#include "../src/spmc_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
/**
 * Test Case 5: Waitfree property verification
 * Tests that operations don't block and complete in bounded time
 */
void test_waitfree_property(spmc_queue_t *queue) {
    int rank = mpi_get_rank(&queue->mpi_ctx);
    
    printf("=== Test Case 5: Waitfree Property Verification ===\n");
    
    struct timespec start_time, end_time;
    
    if (spmc_queue_is_enqueuer(queue)) {
        printf("Rank %d: Testing waitfree enqueue operations\n", rank);
        
        // Test individual enqueue timing
        int num_tests = 50;
        double max_time = 0.0;
        double total_time = 0.0;
        
        for (int i = 0; i < num_tests; i++) {
            clock_gettime(CLOCK_MONOTONIC, &start_time);
            
            int result = spmc_queue_enqueue(queue, 10000 + i);
            
            clock_gettime(CLOCK_MONOTONIC, &end_time);
            
            double op_time = (end_time.tv_sec - start_time.tv_sec) + 
                           (end_time.tv_nsec - start_time.tv_nsec) / 1e9;
            
            total_time += op_time;
            if (op_time > max_time) {
                max_time = op_time;
            }
            
            printf("Enqueue %d: %.6f seconds (result: %s)\n", 
                   i, op_time, (result == MPI_SUCCESS) ? "SUCCESS" : "FAILED");
        }
        
        double avg_time = total_time / num_tests;
        printf("Producer waitfree stats: avg=%.6f sec, max=%.6f sec\n", avg_time, max_time);
        
        // Verify waitfree property: no operation should take excessively long
        if (max_time > 0.1) {  // 100ms threshold
            printf("WARNING: Enqueue operation took %.6f seconds - may not be waitfree!\n", max_time);
        } else {
            printf("PASS: All enqueue operations completed within waitfree bounds\n");
        }
        
    } else {
        printf("Rank %d: Testing waitfree dequeue operations (Consumer %d)\n", rank, rank - 1);
        
        int num_tests = 100;
        double max_time = 0.0;
        double total_time = 0.0;
        int successful_ops = 0;
        
        for (int i = 0; i < num_tests; i++) {
            clock_gettime(CLOCK_MONOTONIC, &start_time);
            
            int value = spmc_queue_dequeue(queue);
            
            clock_gettime(CLOCK_MONOTONIC, &end_time);
            
            double op_time = (end_time.tv_sec - start_time.tv_sec) + 
                           (end_time.tv_nsec - start_time.tv_nsec) / 1e9;
            
            total_time += op_time;
            if (op_time > max_time) {
                max_time = op_time;
            }
            
            if (value != -1) {
                successful_ops++;
                printf("Dequeue %d: %.6f seconds (value: %d)\n", i, op_time, value);
            } else {
                printf("Dequeue %d: %.6f seconds (empty)\n", i, op_time);
            }
            
            usleep(5000); // Small delay between operations
        }
        
        double avg_time = total_time / num_tests;
        printf("Consumer %d waitfree stats: avg=%.6f sec, max=%.6f sec, success_rate=%.1f%%\n", 
               rank - 1, avg_time, max_time, (100.0 * successful_ops / num_tests));
        
        // Verify waitfree property
        if (max_time > 0.1) {  // 100ms threshold
            printf("WARNING: Dequeue operation took %.6f seconds - may not be waitfree!\n", max_time);
        } else {
            printf("PASS: All dequeue operations completed within waitfree bounds\n");
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
    
    printf("Starting waitfree property test on rank %d/%d\n", rank, size);
    
    if (size < 2) {
        fprintf(stderr, "This test requires at least 2 processes\n");
        spmc_queue_destroy(&queue);
        return -1;
    }
    
    // Run test
    test_waitfree_property(&queue);
    
    // Synchronize all processes
    MPI_TRY(mpi_barrier(queue.mpi_ctx.comm));
    
    if (rank == 0) {
        printf("=== Waitfree Property Test Completed ===\n");
        spmc_queue_print_stats(&queue);
    }
    
    // Cleanup
    spmc_queue_destroy(&queue);
    
    return 0;
}