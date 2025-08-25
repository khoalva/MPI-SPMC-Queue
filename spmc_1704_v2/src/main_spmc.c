#include "spmc_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/**
 * Demonstration of SPMC queue using the new MPI Library.
 * This shows the clean and robust interface provided by the new unified library.
 */
int main(int argc, char *argv[]) {
    spmc_queue_t queue;
    
    // Initialize queue using the new library
    MPI_TRY(spmc_queue_init(&queue, argc, argv));
    
    // Print library information
    mpi_print_info(&queue.mpi_ctx);
    spmc_queue_print_stats(&queue);
    
    // Start concurrent producer/consumer operation
    printf("\n=== Concurrent Producer/Consumer Operation ===\n");
    
    if (spmc_queue_is_enqueuer(&queue)) {
        // Producer: Continuously enqueue items while consumers work
        printf("Rank %d: Starting as PRODUCER\n", mpi_get_rank(&queue.mpi_ctx));
        int values[] = {100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 
                        101, 201, 301, 401, 501, 601, 701, 801, 901, 950};
        int num_values = sizeof(values) / sizeof(values[0]);
        
        for (int i = 0; i < num_values; i++) {
            MPI_TRY(spmc_queue_enqueue(&queue, values[i]));
            // Small delay to allow consumers to work concurrently
            usleep(25000); // 25ms - faster than consumers to build up queue
        }
        
        printf("Rank %d: Producer completed enqueuing %d items\n", 
               mpi_get_rank(&queue.mpi_ctx), num_values);
        
        // Producer only produces - waiting for consumers to finish
        printf("Rank %d: Producer finished - waiting for consumers to complete work\n", 
               mpi_get_rank(&queue.mpi_ctx));
        
    } else {
        // Consumer: Continuously try to dequeue items
        printf("Rank %d: Starting as CONSUMER\n", mpi_get_rank(&queue.mpi_ctx));
        
        int items_consumed = 0;
        int consecutive_failures = 0;
        int max_consecutive_failures = 3; // Stop after 5 consecutive failures
        
        while (consecutive_failures < max_consecutive_failures) {
            int value = spmc_queue_dequeue(&queue);
            if (value != -1) {
                items_consumed++;
                consecutive_failures = 0; // Reset failure counter on success
                printf("Rank %d: Successfully consumed item %d (total: %d)\n", 
                       mpi_get_rank(&queue.mpi_ctx), value, items_consumed);
                // Shorter delay when successfully consuming
                usleep(40000); // 40ms
            } else {
                consecutive_failures++;
                printf("Rank %d: Failed to dequeue (consecutive failures: %d/%d)\n", 
                       mpi_get_rank(&queue.mpi_ctx), consecutive_failures, max_consecutive_failures);
                // Small delay when queue is empty, then try again
                usleep(50000); // 50ms - longer retry delay when failing
            }                                                                                            
        }
        
        printf("Rank %d: Consumer finished, consumed %d items (stopped after %d consecutive failures)\n", 
               mpi_get_rank(&queue.mpi_ctx), items_consumed, consecutive_failures);
    }
    
    // Final synchronization to ensure all processes finish
    printf("Rank %d: Waiting for all processes to complete...\n", mpi_get_rank(&queue.mpi_ctx));
    MPI_TRY(mpi_barrier(queue.mpi_ctx.comm));
    
    // Print final statistics
    if (spmc_queue_is_enqueuer(&queue)) {
        printf("\n=== Final Statistics ===\n");
        spmc_queue_print_stats(&queue);
        printf("SPMC Queue demonstration completed successfully!\n");
    }
    
    // Cleanup
    spmc_queue_destroy(&queue);
    
    return 0;
}
