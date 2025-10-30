#define _POSIX_C_SOURCE 199309L
#include "../src/spmc_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
/**
 * Test Case 1: Basic functionality - Single producer, single consumer
 * Verifies basic enqueue/dequeue operations work correctly
 */
void test_basic_enqueue_dequeue(spmc_queue_t *queue) {
    int rank = mpi_get_rank(&queue->mpi_ctx);
    
    printf("=== Test Case 1: Basic Enqueue/Dequeue ===\n");
    
    if (spmc_queue_is_enqueuer(queue)) {
        printf("Rank %d: Testing basic enqueue operations\n", rank);
        
        // Test enqueuing sequential values
        int test_values[] = {1, 2, 3, 4, 5};
        int num_values = sizeof(test_values) / sizeof(test_values[0]);
        
        for (int i = 0; i < num_values; i++) {
            int result = spmc_queue_enqueue(queue, test_values[i]);
            if (result != MPI_SUCCESS) {
                printf("ERROR: Failed to enqueue value %d\n", test_values[i]);
                return;
            }
            printf("Successfully enqueued: %d\n", test_values[i]);
        }
        
        printf("Producer: Enqueued %d values successfully\n", num_values);
        
    } else {
        printf("Rank %d: Testing basic dequeue operations\n", rank);
        
        int dequeued_count = 0;
        int max_attempts = 20;
        
        for (int attempt = 0; attempt < max_attempts; attempt++) {
            int value = spmc_queue_dequeue(queue);
            if (value != -1) {
                printf("Successfully dequeued: %d\n", value);
                dequeued_count++;
            } else {
                usleep(10000); // 10ms delay before retry
            }
        }
        
        printf("Consumer: Dequeued %d values\n", dequeued_count);
    }
}

int main(int argc, char *argv[]) {
    spmc_queue_t queue;
    
    // Initialize queue
    if (spmc_queue_init(&queue, argc, argv) != MPI_SUCCESS) {
        fprintf(stderr, "Failed to initialize SPMC queue\n");
        return -1;
    }
    
    // Print test info
    int rank = mpi_get_rank(&queue.mpi_ctx);
    int size = mpi_get_size(&queue.mpi_ctx);
    
    printf("Starting basic SPMC queue test on rank %d/%d\n", rank, size);
    
    // Ensure we have at least 2 processes
    if (size < 2) {
        fprintf(stderr, "This test requires at least 2 processes (1 producer, 1+ consumer)\n");
        spmc_queue_destroy(&queue);
        return -1;
    }
    
    // Run test
    test_basic_enqueue_dequeue(&queue);
    
    // Synchronize all processes
    MPI_TRY(mpi_barrier(queue.mpi_ctx.comm));
    
    if (rank == 0) {
        printf("=== Basic Test Completed ===\n");
        spmc_queue_print_stats(&queue);
    }
    
    // Cleanup
    spmc_queue_destroy(&queue);
    
    return 0;
}