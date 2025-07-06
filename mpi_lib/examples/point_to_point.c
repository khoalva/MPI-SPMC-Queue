#include "mpi_lib.h"

#define MESSAGE_TAG 100
#define DATA_SIZE 10

int main(int argc, char *argv[]) {
    mpi_context_t ctx;
    
    // Initialize MPI
    if (mpi_init(argc, argv, &ctx) != MPI_SUCCESS) {
        return 1;
    }
    
    if (mpi_get_size(&ctx) < 2) {
        if (mpi_is_root(&ctx)) {
            printf("This example requires at least 2 processes\n");
        }
        mpi_finalize();
        return 1;
    }
    
    int data[DATA_SIZE];
    
    if (mpi_is_root(&ctx)) {
        // Root process sends data to all other processes
        printf("Root process sending data to all other processes...\n");
        
        for (int i = 0; i < DATA_SIZE; i++) {
            data[i] = i * 10;
        }
        
        for (int dest = 1; dest < mpi_get_size(&ctx); dest++) {
            printf("Sending to rank %d: ", dest);
            for (int i = 0; i < DATA_SIZE; i++) {
                printf("%d ", data[i]);
            }
            printf("\n");
            
            mpi_send(data, DATA_SIZE, MPI_LIB_INT, 
                     dest, MESSAGE_TAG, ctx.comm);
        }
        printf("All sends completed!\n");
    } else {
        // Other processes receive data from root
        MPI_Status status;
        mpi_recv(data, DATA_SIZE, MPI_LIB_INT, 
                 0, MESSAGE_TAG, ctx.comm, &status);
        
        printf("Rank %d received: ", mpi_get_rank(&ctx));
        for (int i = 0; i < DATA_SIZE; i++) {
            printf("%d ", data[i]);
        }
        printf("\n");
    }
    
    // Synchronize all processes
    mpi_barrier(ctx.comm);
    
    if (mpi_is_root(&ctx)) {
        printf("Point-to-point communication example completed!\n");
    }
    
    mpi_finalize();
    return 0;
}
