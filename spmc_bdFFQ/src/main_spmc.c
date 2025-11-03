#define _POSIX_C_SOURCE 199309L
#define _GNU_SOURCE
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
        int max_attempts = 50; // More attempts to catch items as they're produced
        
        for (int attempt = 0; attempt < max_attempts; attempt++) {
            int buffer[10]; // Buffer for batch dequeue
            int count = spmc_queue_dequeue(&queue, buffer, 10);
            
            if (count > 0) {
                items_consumed += count;
                for (int i = 0; i < count; i++) {
                    printf("%d ", buffer[i]);
                }
                // Shorter delay when successfully consuming
                usleep(40000); // 40ms
            } else {
                // Small delay when queue is empty, then try again
                usleep(20000); // 20ms - quick retry
            }
        }
        
        printf("Rank %d: Consumer finished, consumed %d items\n", 
               mpi_get_rank(&queue.mpi_ctx), items_consumed);
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
