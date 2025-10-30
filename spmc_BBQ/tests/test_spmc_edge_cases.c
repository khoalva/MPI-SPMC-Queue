#define _POSIX_C_SOURCE 199309L
#include "../src/spmc_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <limits.h>
/**
 * Test Case 4: Edge cases and boundary conditions
 * Tests queue behavior at limits and special conditions
 */
void test_edge_cases(spmc_queue_t *queue) {
    int rank = mpi_get_rank(&queue->mpi_ctx);
    
    printf("=== Test Case 4: Edge Cases and Boundary Conditions ===\n");
    
    if (spmc_queue_is_enqueuer(queue)) {
        printf("Rank %d: Testing edge cases for producer\n", rank);
        
        // Test 1: Enqueue zero and negative values
        printf("Test 1: Enqueuing special values (0, negative)\n");
        int special_values[] = {0, -1, -100, INT_MIN, INT_MAX};
        int num_special = sizeof(special_values) / sizeof(special_values[0]);
        
        for (int i = 0; i < num_special; i++) {
            int result = spmc_queue_enqueue(queue, special_values[i]);
            printf("Enqueued special value %d: %s\n", 
                   special_values[i], (result == MPI_SUCCESS) ? "SUCCESS" : "FAILED");
        }
        
        // Test 2: Rapid burst enqueue
        printf("Test 2: Rapid burst enqueuing\n");
        int burst_size = 20;
        for (int i = 0; i < burst_size; i++) {
            int result = spmc_queue_enqueue(queue, 1000 + i);
            if (result != MPI_SUCCESS) {
                printf("WARN: Burst enqueue failed at item %d\n", i);
            }
        }
        printf("Completed burst of %d enqueues\n", burst_size);
        
        // Test 3: Delayed enqueue pattern
        printf("Test 3: Delayed enqueue pattern\n");
        for (int i = 0; i < 10; i++) {
            spmc_queue_enqueue(queue, 2000 + i);
            usleep(50000); // 50ms delay between enqueues
        }
        
        // Test 4: Large gap in values
        printf("Test 4: Large value gaps\n");
        int large_values[] = {1000000, 2000000, 0, 5000000};
        for (int i = 0; i < 4; i++) {
            spmc_queue_enqueue(queue, large_values[i]);
        }
        
    } else {
        printf("Rank %d: Testing edge cases for consumer %d\n", rank, rank - 1);
        
        // Test 1: Dequeue from empty queue
        printf("Test 1: Dequeuing from potentially empty queue\n");
        for (int i = 0; i < 5; i++) {
            int value = spmc_queue_dequeue(queue);
            printf("Empty queue dequeue attempt %d: %s (value=%d)\n", 
                   i + 1, (value == -1) ? "EMPTY" : "GOT_VALUE", value);
            usleep(10000);
        }
        
        // Test 2: Consumer persistence test
        printf("Test 2: Consumer persistence under varying conditions\n");
        int values_found = 0;
        int empty_count = 0;
        int max_attempts = 100;
        
        for (int attempt = 0; attempt < max_attempts; attempt++) {
            int value = spmc_queue_dequeue(queue);
            if (value != -1) {
                values_found++;
                empty_count = 0;
                printf("Consumer %d found value: %d (total found: %d)\n", 
                       rank - 1, value, values_found);
            } else {
                empty_count++;
                if (empty_count > 10) {
                    usleep(20000); // Longer delay after many empty reads
                } else {
                    usleep(5000);  // Short delay for quick retry
                }
            }
        }
        
        printf("Consumer %d edge case results: found=%d values, final empty_streak=%d\n", 
               rank - 1, values_found, empty_count);
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
    
    printf("Starting edge cases test on rank %d/%d\n", rank, size);
    
    if (size < 2) {
        fprintf(stderr, "This test requires at least 2 processes\n");
        spmc_queue_destroy(&queue);
        return -1;
    }
    
    // Run test
    test_edge_cases(&queue);
    
    // Synchronize all processes
    MPI_TRY(mpi_barrier(queue.mpi_ctx.comm));
    
    if (rank == 0) {
        printf("=== Edge Cases Test Completed ===\n");
        spmc_queue_print_stats(&queue);
    }
    
    // Cleanup
    spmc_queue_destroy(&queue);
    
    return 0;
}