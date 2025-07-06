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
    
    // Enqueuer (rank 0) enqueues some values
    if (spmc_queue_is_enqueuer(&queue)) {
        printf("\n=== Enqueuing Phase ===\n");
        int values[] = {100, 200, 300, 400, 500, 600, 700, 800};
        int num_values = sizeof(values) / sizeof(values[0]);
        
        for (int i = 0; i < num_values; i++) {
            MPI_TRY(spmc_queue_enqueue(&queue, values[i]));
            // Small delay to simulate work
            usleep(50000); // 50ms
        }
        
        printf("Enqueuing completed.\n");
    }
    
    // Synchronize all processes
    MPI_TRY(mpi_barrier(queue.mpi_ctx.comm));
    
    // All processes try to dequeue
    printf("\n=== Dequeuing Phase ===\n");
    for (int attempt = 0; attempt < 5; attempt++) {
        int value = spmc_queue_dequeue(&queue);
        if (value == -1) {
            printf("Rank %d: No more items available\n", mpi_get_rank(&queue.mpi_ctx));
            break;
        }
        // Small delay between dequeue attempts
        usleep(30000); // 30ms
    }
    
    // Final synchronization
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
