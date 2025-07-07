#include "mpi_lib.h"
#include <stdlib.h>
#include <time.h>

#define ARRAY_SIZE 10

int main(int argc, char *argv[]) {
    mpi_context_t ctx;
    
    // Initialize MPI
    MPI_TRY(mpi_init(argc, argv, &ctx));
    
    int rank = mpi_get_rank(&ctx);
    int size = mpi_get_size(&ctx);
    
    // Seed random number generator
    srand(time(NULL) + rank);
    
    // Create local array with random values
    int local_data[ARRAY_SIZE];
    for (int i = 0; i < ARRAY_SIZE; i++) {
        local_data[i] = rand() % 100;
    }
    
    printf("Rank %d local data: ", rank);
    for (int i = 0; i < ARRAY_SIZE; i++) {
        printf("%d ", local_data[i]);
    }
    printf("\n");
    
    // Broadcast from root
    int broadcast_value = 42;
    if (mpi_is_root(&ctx)) {
        printf("Root broadcasting value: %d\n", broadcast_value);
    }
    
    MPI_TRY(mpi_bcast(&broadcast_value, 1, MPI_INT, 0, ctx.comm));
    
    if (!mpi_is_root(&ctx)) {
        printf("Rank %d received broadcast value: %d\n", rank, broadcast_value);
    }
    
    // Calculate local sum
    int local_sum = 0;
    for (int i = 0; i < ARRAY_SIZE; i++) {
        local_sum += local_data[i];
    }
    printf("Rank %d local sum: %d\n", rank, local_sum);
    
    // Reduce to find global sum
    int global_sum = 0;
    MPI_TRY(mpi_reduce(&local_sum, &global_sum, 1, MPI_INT, 
                       MPI_SUM, 0, ctx.comm));
    
    if (mpi_is_root(&ctx)) {
        printf("Global sum of all arrays: %d\n", global_sum);
        printf("Average per process: %.2f\n", (double)global_sum / size);
    }
    
    // Find maximum value across all processes
    int local_max = local_data[0];
    for (int i = 1; i < ARRAY_SIZE; i++) {
        if (local_data[i] > local_max) {
            local_max = local_data[i];
        }
    }
    
    int global_max = 0;
    MPI_TRY(mpi_allreduce(&local_max, &global_max, 1, MPI_INT, 
                          MPI_MAX, ctx.comm));
    
    printf("Rank %d: local max = %d, global max = %d\n", rank, local_max, global_max);
    
    // Calculate global average of each array position
    int global_averages[ARRAY_SIZE];
    MPI_TRY(mpi_allreduce(local_data, global_averages, ARRAY_SIZE, 
                          MPI_INT, MPI_SUM, ctx.comm));
    
    if (mpi_is_root(&ctx)) {
        printf("Global averages by position: ");
        for (int i = 0; i < ARRAY_SIZE; i++) {
            printf("%.1f ", (double)global_averages[i] / size);
        }
        printf("\n");
    }
    
    // Final synchronization
    MPI_TRY(mpi_barrier(ctx.comm));
    
    if (mpi_is_root(&ctx)) {
        printf("Collective communication example completed!\n");
    }
    
    mpi_finalize();
    return 0;
}
