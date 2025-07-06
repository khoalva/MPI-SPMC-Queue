#include "mpi_lib.h"
#include <stdio.h>
#include <stdlib.h>

/**
 * Example demonstrating one-sided communication with MPI windows
 */
int main(int argc, char *argv[]) {
    mpi_context_t ctx;
    
    // Initialize MPI
    MPI_CHECK(mpi_init(argc, argv, &ctx));
    
    const int ARRAY_SIZE = 10;
    int *shared_array = NULL;
    mpi_window_t win;
    
    // Allocate memory on root
    shared_array = (int*)mpi_calloc(ARRAY_SIZE * sizeof(int), 0, ctx.rank);
    
    // Create window
    MPI_CHECK(mpi_win_create(shared_array, 
                            mpi_is_root(&ctx) ? ARRAY_SIZE * sizeof(int) : 0,
                            sizeof(int), ctx.comm, &win));
    
    // Lock window for passive target operations
    MPI_CHECK(mpi_win_lock_all(&win));
    
    if (mpi_is_root(&ctx)) {
        printf("Root initializing shared array...\n");
        for (int i = 0; i < ARRAY_SIZE; i++) {
            shared_array[i] = i;
        }
        printf("Array initialized: ");
        for (int i = 0; i < ARRAY_SIZE; i++) {
            printf("%d ", shared_array[i]);
        }
        printf("\n");
    }
    
    // Synchronize
    mpi_barrier(ctx.comm);
    
    // Each non-root process reads and modifies one element
    if (!mpi_is_root(&ctx)) {
        int index = ctx.rank % ARRAY_SIZE;
        int old_value;
        
        // Read current value
        MPI_CHECK(mpi_get(&old_value, 1, MPI_INT, 0, index * sizeof(int), &win));
        printf("Rank %d read value %d from index %d\n", ctx.rank, old_value, index);
        
        // Write new value
        int new_value = old_value + ctx.rank * 100;
        MPI_CHECK(mpi_put(&new_value, 1, MPI_INT, 0, index * sizeof(int), &win));
        printf("Rank %d wrote value %d to index %d\n", ctx.rank, new_value, index);
    }
    
    // Synchronize
    mpi_barrier(ctx.comm);
    
    if (mpi_is_root(&ctx)) {
        printf("\nFinal array state: ");
        for (int i = 0; i < ARRAY_SIZE; i++) {
            printf("%d ", shared_array[i]);
        }
        printf("\n");
    }
    
    // Cleanup
    MPI_CHECK(mpi_win_unlock_all(&win));
    mpi_win_destroy(&win);
    mpi_free(shared_array, 0, ctx.rank);
    
    mpi_finalize();
    return 0;
}
