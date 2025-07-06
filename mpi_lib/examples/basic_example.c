#include "mpi_lib.h"
#include <stdio.h>

/**
 * Simple example demonstrating basic MPI wrapper usage
 */
int main(int argc, char *argv[]) {
    mpi_context_t ctx;
    
    // Initialize MPI
    int err = mpi_init(argc, argv, &ctx);
    if (err != MPI_SUCCESS) {
        return 1;
    }
    
    // Print basic info
    mpi_print_info(&ctx);
    
    // Simple communication example
    if (mpi_is_root(&ctx)) {
        printf("\nRoot process sending messages...\n");
        for (int i = 1; i < mpi_get_size(&ctx); i++) {
            int message = i * 10;
            mpi_send(&message, 1, MPI_INT, i, 0, ctx.comm);
            printf("Sent %d to rank %d\n", message, i);
        }
    } else {
        int received;
        mpi_recv(&received, 1, MPI_INT, 0, 0, ctx.comm, MPI_STATUS_IGNORE);
        printf("Rank %d received: %d\n", ctx.rank, received);
    }
    
    // Synchronize all processes
    mpi_barrier(ctx.comm);
    
    if (mpi_is_root(&ctx)) {
        printf("\nAll processes synchronized!\n");
    }
    
    // Cleanup
    mpi_finalize();
    return 0;
}
