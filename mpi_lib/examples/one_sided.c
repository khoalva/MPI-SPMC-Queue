#include "mpi_lib.h"

#define ARRAY_SIZE 10

int main(int argc, char *argv[]) {
    mpi_easy_context_t ctx;
    
    // Initialize MPI
    MPI_EASY_TRY(mpi_easy_init(argc, argv, &ctx));
    
    if (mpi_easy_get_size(&ctx) < 2) {
        if (mpi_easy_is_root(&ctx)) {
            printf("This example requires at least 2 processes\n");
        }
        mpi_easy_finalize();
        return 1;
    }
    
    // Allocate memory for shared array (only on root)
    int *shared_array = mpi_easy_calloc(ARRAY_SIZE * sizeof(int), 0, mpi_easy_get_rank(&ctx));
    
    // Create MPI window
    mpi_easy_window_t win;
    MPI_EASY_TRY(mpi_easy_win_create(shared_array, 
                                     mpi_easy_is_root(&ctx) ? ARRAY_SIZE * sizeof(int) : 0,
                                     sizeof(int), ctx.comm, &win));
    
    // Lock window for passive target synchronization
    MPI_EASY_TRY(mpi_easy_win_lock_all(&win));
    
    if (mpi_easy_is_root(&ctx)) {
        // Initialize array
        printf("Root initializing shared array...\n");
        for (int i = 0; i < ARRAY_SIZE; i++) {
            shared_array[i] = i;
        }
        printf("Initial array: ");
        for (int i = 0; i < ARRAY_SIZE; i++) {
            printf("%d ", shared_array[i]);
        }
        printf("\n");
    }
    
    // Synchronize
    MPI_EASY_TRY(mpi_easy_barrier(ctx.comm));
    
    if (!mpi_easy_is_root(&ctx)) {
        // Non-root processes read and modify the array
        int rank = mpi_easy_get_rank(&ctx);
        int index = rank % ARRAY_SIZE;
        int old_value, new_value = rank * 100;
        
        // Read current value
        MPI_EASY_TRY(mpi_easy_get(&old_value, 1, MPI_EASY_INT, 0, index * sizeof(int), &win));
        printf("Rank %d read value %d from index %d\n", rank, old_value, index);
        
        // Compare and swap
        int compare_value = old_value;
        int result;
        MPI_EASY_TRY(mpi_easy_compare_and_swap(&new_value, &compare_value, &result, 
                                               MPI_EASY_INT, 0, index * sizeof(int), &win));
        
        if (result == compare_value) {
            printf("Rank %d successfully updated index %d: %d -> %d\n", 
                   rank, index, old_value, new_value);
        } else {
            printf("Rank %d failed to update index %d (current value: %d)\n", 
                   rank, index, result);
        }
    }
    
    // Synchronize before final read
    MPI_EASY_TRY(mpi_easy_barrier(ctx.comm));
    
    if (mpi_easy_is_root(&ctx)) {
        printf("Final array: ");
        for (int i = 0; i < ARRAY_SIZE; i++) {
            printf("%d ", shared_array[i]);
        }
        printf("\n");
    }
    
    // Clean up
    MPI_EASY_TRY(mpi_easy_win_unlock_all(&win));
    mpi_easy_win_destroy(&win);
    mpi_easy_free(shared_array, 0, mpi_easy_get_rank(&ctx));
    
    if (mpi_easy_is_root(&ctx)) {
        printf("One-sided communication example completed!\n");
    }
    
    mpi_easy_finalize();
    return 0;
}
