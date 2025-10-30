#define _POSIX_C_SOURCE 199309L
#include "../src/spmc_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>

/**
 * Test Case: Consumer Overtaking Producer with Row Jumping
 * This test simulates scenarios where consumers read faster than producer writes,
 * forcing consumers to jump to new rows in the bitmap structure.
 */

void test_consumer_overtaking_scenario(spmc_queue_t *queue) {
    int rank = mpi_get_rank(&queue->mpi_ctx);
    int size = mpi_get_size(&queue->mpi_ctx);
    
    printf("=== Consumer Overtaking Producer Test ===\n");
    
    if (spmc_queue_is_enqueuer(queue)) {
        printf("Rank %d: Producer starting with DELIBERATE DELAYS\n", rank);
        
        // Phase 1: Fill queue with moderate items
        printf("Producer: Phase 1 - Fill queue moderately (25 items)\n");
        for (int i = 1; i <= 25; i++) {
            int result = spmc_queue_enqueue(queue, i);
            if (result == MPI_SUCCESS) {
                printf("Producer: Enqueued %d\n", i);
            }
            usleep(20000); // 20ms delay - moderate speed to fill queue
        }
        
        // Give consumers time to start consuming some items
        printf("Producer: Brief pause to let consumers start consuming...\n");
        usleep(300000); // 300ms pause - let consumers consume some items
        
        // Phase 2: SLOW DOWN significantly - consumers should still be consuming from phase 1
        printf("Producer: Phase 2 - VERY SLOW (next 25 items with extreme delays)\n");
        for (int i = 26; i <= 50; i++) {
            printf("Producer: About to enqueue %d after long delay...\n", i);
            usleep(800000); // 800ms delay - very slow but not too extreme
            
            int result = spmc_queue_enqueue(queue, i);
            if (result == MPI_SUCCESS) {
                printf("Producer: Enqueued %d (slow phase)\n", i);
            }
        }
        
        // Phase 3: Speed up again
        printf("Producer: Phase 3 - Speed up again (next 15 items)\n");
        for (int i = 51; i <= 65; i++) {
            int result = spmc_queue_enqueue(queue, i);
            if (result == MPI_SUCCESS) {
                printf("Producer: Enqueued %d (fast again)\n", i);
            }
            usleep(30000); // 30ms delay
        }
        
        // Add sentinels
        for (int i = 0; i < size - 1; i++) {
            spmc_queue_enqueue(queue, -9999);
        }
        
        printf("Producer: Finished with %d sentinels\n", size - 1);
        
    } else {
        int consumer_id = rank - 1;
        printf("Rank %d: Consumer %d starting (will try to overtake producer)\n", 
               rank, consumer_id);
        
        int consumed_values[100];
        int consumed_count = 0;
        int empty_reads = 0;
        int consecutive_empty = 0;
        int max_attempts = 500;  // Increase attempts to give more time
        
        printf("Consumer %d: Starting aggressive consumption mode\n", consumer_id);
        
        for (int attempt = 0; attempt < max_attempts; attempt++) {
            // Add debug output every 50 attempts to see what consumer is doing
            if (attempt % 50 == 0 && attempt > 0) {
                printf("Consumer %d: Attempt %d, total consumed so far: %d, empty_reads: %d\n", 
                       consumer_id, attempt, consumed_count, empty_reads);
            }
            
            int value = spmc_queue_dequeue(queue);
            
            if (value == -9999) {
                printf("Consumer %d: Found sentinel, stopping\n", consumer_id);
                break;
            } else if (value != -1) {
                // Got a value
                if (consumed_count < 100) {
                    consumed_values[consumed_count] = value;
                }
                consumed_count++;
                consecutive_empty = 0;
                
                printf("Consumer %d: Consumed value %d (total: %d) [attempt %d]\n", 
                       consumer_id, value, consumed_count, attempt);
                
                // Minimal delay for very fast consumption
                usleep(1000); // 1ms delay - extremely fast
                
            } else {
                // Empty read - consumer might be overtaking producer
                empty_reads++;
                consecutive_empty++;
                
                if (consecutive_empty % 5 == 0) {
                    printf("Consumer %d: %d consecutive empty reads - overtaking producer? [attempt %d, total consumed: %d]\n", 
                           consumer_id, consecutive_empty, attempt, consumed_count);
                }
                
                // Much shorter delays when empty to catch up faster
                if (consecutive_empty > 30) {
                    usleep(10000); // 10ms delay when many consecutive empties
                } else if (consecutive_empty > 10) {
                    usleep(2000);  // 2ms delay for moderate empties  
                } else {
                    usleep(500);   // 0.5ms delay for few empties - keep trying very fast
                }
            }
        }
        
        printf("\nConsumer %d final stats:\n", consumer_id);
        printf("  - Items consumed: %d\n", consumed_count);
        printf("  - Empty reads: %d\n", empty_reads);
        printf("  - Empty read ratio: %.2f%%\n", 
               (float)empty_reads / (empty_reads + consumed_count) * 100);
        
        // Analyze consumption pattern
        printf("  - First 10 consumed values: ");
        for (int i = 0; i < 10 && i < consumed_count; i++) {
            printf("%d ", consumed_values[i]);
        }
        printf("\n");
        
        if (empty_reads > consumed_count) {
            printf("Consumer %d: HIGH empty read ratio - successfully overtook producer!\n", consumer_id);
        } else {
            printf("Consumer %d: Normal consumption pattern\n", consumer_id);
        }
    }
}

void test_row_jumping_stress(spmc_queue_t *queue) {
    int rank = mpi_get_rank(&queue->mpi_ctx);
    int size = mpi_get_size(&queue->mpi_ctx);
    
    printf("\n=== Row Jumping Stress Test ===\n");
    
    if (spmc_queue_is_enqueuer(queue)) {
        printf("Rank %d: Producer starting row jumping stress test\n", rank);
        
        // Strategy: Burst writes followed by long pauses
        // This forces consumers to exhaust current rows and jump to new ones
        
        for (int burst = 0; burst < 5; burst++) {
            printf("Producer: Burst %d - writing 8 items quickly\n", burst + 1);
            
            // Quick burst of items
            for (int i = 0; i < 8; i++) {
                int value = burst * 10 + i + 1;
                int result = spmc_queue_enqueue(queue, value);
                if (result == MPI_SUCCESS) {
                    printf("Producer: Burst %d item %d -> %d\n", burst + 1, i + 1, value);
                }
                usleep(25000); // 25ms between items in burst
            }
            
            // Long pause to let consumers consume everything and wait
            printf("Producer: Long pause after burst %d (500ms)\n", burst + 1);
            usleep(500000); // 500ms pause
        }
        
        // Final cleanup
        for (int i = 0; i < size - 1; i++) {
            spmc_queue_enqueue(queue, -9999);
        }
        
        printf("Producer: Row jumping stress test completed\n");
        
    } else {
        int consumer_id = rank - 1;
        printf("Rank %d: Consumer %d starting row jumping stress test\n", 
               rank, consumer_id);
        
        int consumed_per_burst[10] = {0}; // Track consumption per burst
        int total_consumed = 0;
        int current_burst = 0;
        int row_jumps_detected = 0;
        int max_attempts = 400;
        
        for (int attempt = 0; attempt < max_attempts; attempt++) {
            int value = spmc_queue_dequeue(queue);
            
            if (value == -9999) {
                printf("Consumer %d: Found sentinel\n", consumer_id);
                break;
            } else if (value != -1) {
                total_consumed++;
                
                // Determine which burst this belongs to
                int burst_id = (value - 1) / 10;
                if (burst_id >= 0 && burst_id < 10) {
                    consumed_per_burst[burst_id]++;
                    
                    if (burst_id > current_burst) {
                        row_jumps_detected++;
                        printf("Consumer %d: Detected jump to burst %d (potential row jump)\n", 
                               consumer_id, burst_id + 1);
                        current_burst = burst_id;
                    }
                }
                
                printf("Consumer %d: Got %d (burst %d)\n", consumer_id, value, burst_id + 1);
                usleep(15000); // 15ms - fast consumption
                
            } else {
                // Empty read
                usleep(30000); // 30ms delay on empty
            }
        }
        
        printf("\nConsumer %d row jumping analysis:\n", consumer_id);
        printf("  - Total consumed: %d\n", total_consumed);
        printf("  - Row jumps detected: %d\n", row_jumps_detected);
        printf("  - Consumption per burst: ");
        for (int i = 0; i < 5; i++) {
            printf("B%d:%d ", i + 1, consumed_per_burst[i]);
        }
        printf("\n");
        
        if (row_jumps_detected >= 2) {
            printf("Consumer %d: GOOD - Multiple row jumps detected!\n", consumer_id);
        } else {
            printf("Consumer %d: Limited row jumping observed\n", consumer_id);
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
    
    printf("Starting consumer overtaking test on rank %d/%d\n", rank, size);
    
    if (size < 2) {
        fprintf(stderr, "This test requires at least 2 processes\n");
        spmc_queue_destroy(&queue);
        return -1;
    }
    
    // Test 1: Consumer overtaking scenario
    test_consumer_overtaking_scenario(&queue);
    
    // Synchronize before next test
    MPI_TRY(mpi_barrier(queue.mpi_ctx.comm));
    usleep(100000); // 100ms pause between tests
    
    // Test 2: Row jumping stress test
    test_row_jumping_stress(&queue);
    
    // Final synchronization
    MPI_TRY(mpi_barrier(queue.mpi_ctx.comm));
    
    if (rank == 0) {
        printf("\n=== Consumer Overtaking Tests Completed ===\n");
        spmc_queue_print_stats(&queue);
    }
    
    // Cleanup
    spmc_queue_destroy(&queue);
    
    return 0;
}