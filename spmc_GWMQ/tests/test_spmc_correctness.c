#define _POSIX_C_SOURCE 199309L
#include "../src/spmc_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>

/**
 * Test Case 6: Correctness verification
 * Tests that data integrity is maintained and no items are lost or duplicated
 */
void test_correctness(spmc_queue_t *queue) {
    int rank = mpi_get_rank(&queue->mpi_ctx);
    int size = mpi_get_size(&queue->mpi_ctx);
    
    printf("=== Test Case 6: Correctness Verification ===\n");
    
    if (spmc_queue_is_enqueuer(queue)) {
        printf("Rank %d: Producer generating sequence for correctness test\n", rank);
        
        // Generate a known sequence that consumers can verify
        int start_value = 1000;
        int num_items = 100;
        
        printf("Producer: Generating sequence from %d to %d\n", 
               start_value, start_value + num_items - 1);
        
        for (int i = 0; i < num_items; i++) {
            int value = start_value + i;
            int result = spmc_queue_enqueue(queue, value);
            
            if (result != MPI_SUCCESS) {
                printf("ERROR: Failed to enqueue value %d in sequence\n", value);
            } else if (i % 10 == 0) {
                printf("Producer: Enqueued %d items so far...\n", i + 1);
            }
            
            // Small delay to allow consumers to process
            usleep(2000); // 2ms
        }
        
        // Add sentinel values to mark end of sequence
        for (int i = 0; i < size - 1; i++) {  // One sentinel per consumer
            spmc_queue_enqueue(queue, -9999);  // Sentinel value
        }
        
        printf("Producer: Finished sequence generation (%d items + %d sentinels)\n", 
               num_items, size - 1);
        
    } else {
        printf("Rank %d: Consumer %d starting correctness verification\n", rank, rank - 1);
        
        int consumed_values[1000];  // Array to store consumed values
        int consumed_count = 0;
        int sentinels_found = 0;
        int max_attempts = 200;
        
        for (int attempt = 0; attempt < max_attempts; attempt++) {
            int value = spmc_queue_dequeue(queue);
            
            if (value == -9999) {  // Sentinel value
                sentinels_found++;
                printf("Consumer %d: Found sentinel #%d\n", rank - 1, sentinels_found);
                break;  // End of data for this consumer
                
            } else if (value != -1) {
                if (consumed_count < 1000) {
                    consumed_values[consumed_count] = value;
                }
                consumed_count++;
                
                if (consumed_count % 10 == 0) {
                    printf("Consumer %d: Consumed %d items...\n", rank - 1, consumed_count);
                }
            }
            
            usleep(5000); // 5ms delay
        }
        
        printf("Consumer %d: Finished consuming %d items\n", rank - 1, consumed_count);
        
        // Verify correctness of consumed data
        printf("Consumer %d: Verifying data correctness...\n", rank - 1);
        
        // Check for duplicates
        int duplicates = 0;
        for (int i = 0; i < consumed_count && i < 1000; i++) {
            for (int j = i + 1; j < consumed_count && j < 1000; j++) {
                if (consumed_values[i] == consumed_values[j]) {
                    duplicates++;
                    printf("DUPLICATE FOUND: Value %d at positions %d and %d\n", 
                           consumed_values[i], i, j);
                }
            }
        }
        
        // Check value range
        int out_of_range = 0;
        int min_val = INT_MAX, max_val = INT_MIN;
        for (int i = 0; i < consumed_count && i < 1000; i++) {
            int val = consumed_values[i];
            if (val < min_val) min_val = val;
            if (val > max_val) max_val = val;
            
            // Expected range: 1000-1099
            if (val < 1000 || val > 1099) {
                out_of_range++;
            }
        }
        
        printf("Consumer %d correctness results:\n", rank - 1);
        printf("  - Items consumed: %d\n", consumed_count);
        printf("  - Duplicates found: %d\n", duplicates);
        printf("  - Out-of-range values: %d\n", out_of_range);
        printf("  - Value range: %d to %d\n", min_val, max_val);
        printf("  - Sentinels found: %d\n", sentinels_found);
        
        if (duplicates == 0 && out_of_range == 0) {
            printf("Consumer %d: PASS - No correctness errors detected\n", rank - 1);
        } else {
            printf("Consumer %d: FAIL - Correctness errors detected\n", rank - 1);
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
    
    printf("Starting correctness verification test on rank %d/%d\n", rank, size);
    
    if (size < 2) {
        fprintf(stderr, "This test requires at least 2 processes\n");
        spmc_queue_destroy(&queue);
        return -1;
    }
    
    // Run test
    test_correctness(&queue);
    
    // Final synchronization
    MPI_TRY(mpi_barrier(queue.mpi_ctx.comm));
    
    if (rank == 0) {
        printf("=== Correctness Verification Test Completed ===\n");
        spmc_queue_print_stats(&queue);
    }
    
    // Cleanup
    spmc_queue_destroy(&queue);
    
    return 0;
}