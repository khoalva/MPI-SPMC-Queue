#include "mpi_lib.h"

int main(int argc, char *argv[]) {
    mpi_context_t ctx;
    
    // Initialize MPI using the wrapper
    if (mpi_init(argc, argv, &ctx) != MPI_SUCCESS) {
        return 1;
    }
    
    // Print library and process information
    mpi_print_info(&ctx);
    
    // Simple hello world message
    printf("Hello from rank %d of %d processes!\n", 
           mpi_get_rank(&ctx), mpi_get_size(&ctx));
    
    // Demonstrate barrier
    if (mpi_is_root(&ctx)) {
        printf("Root process waiting for all processes...\n");
    }
    
    mpi_barrier(ctx.comm);
    
    if (mpi_is_root(&ctx)) {
        printf("All processes synchronized!\n");
    }
    
    // Clean up
    mpi_finalize();
    return 0;
}
