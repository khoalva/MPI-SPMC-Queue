#define _POSIX_C_SOURCE 199309L
#include "../src/spmc_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
/**
 * Test Case 2: Multiple consumers competing for items
 * Tests the waitfree property with multiple consumers
 */
void test_multiple_consumers(spmc_queue_t *queue) {
    int rank = mpi_get_rank(&queue->mpi_ctx);
    int size = mpi_get_size(&queue->mpi_ctx);
    
    printf("=== Test Case 2: Multiple Consumer Competition ===\n");
    
    if (spmc_queue_is_enqueuer(queue)) {
        printf("Rank %d: Producer starting with %d consumers\n", rank, size - 1);
        
        // Produce a large number of items for consumers to compete over
        int num_items = 50;
        printf("Producer: Will produce %d items\n", num_items);
        
        for (int i = 1; i <= num_items; i++) {
            int result = spmc_queue_enqueue(queue, i * 10); // Use distinctive values
            if (result != MPI_SUCCESS) {
                printf("ERROR: Failed to enqueue value %d\n", i * 10);
                break;
            }
            
            // Small delay to allow some consumers to catch up
            if (i % 5 == 0) {
                usleep(5000); // 5ms delay every 5 items
            }
        }
        
        printf("Producer: Finished producing %d items\n", num_items);
        
    } else {
        printf("Rank %d: Consumer %d starting competition\n", rank, rank - 1);
        
        int consumed_count = 0;
        int total_value = 0;
        int max_attempts = 100;
        
        for (int attempt = 0; attempt < max_attempts; attempt++) {
            int value = spmc_queue_dequeue(queue);
            if (value != -1) {
                consumed_count++;
                total_value += value;
                printf("Consumer %d dequeued: %d (total consumed: %d)\n", 
                       rank - 1, value, consumed_count);
            } else {
                usleep(8000); // 8ms delay before retry
            }
        }
        
        printf("Consumer %d final stats: consumed=%d items, total_value=%d\n", 
               rank - 1, consumed_count, total_value);
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
    
    printf("Starting multiple consumer competition test on rank %d/%d\n", rank, size);
    
    // Ensure we have at least 3 processes for meaningful competition
    if (size < 3) {
        fprintf(stderr, "This test requires at least 3 processes (1 producer, 2+ consumers)\n");
        spmc_queue_destroy(&queue);
        return -1;
    }
    
    // Run test
    test_multiple_consumers(&queue);
    
    // Synchronize all processes
    MPI_TRY(mpi_barrier(queue.mpi_ctx.comm));
    
    if (rank == 0) {
        printf("=== Multiple Consumer Competition Test Completed ===\n");
        spmc_queue_print_stats(&queue);
    }
    
    // Cleanup
    spmc_queue_destroy(&queue);
    
    return 0;
}