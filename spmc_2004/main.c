#include "queue.h"
#include <stdio.h>
#include <stdlib.h>

/**
 * Main program to demonstrate the single-enqueuer wait-free queue using MPI one-sided communication.
 * Rank 0 enqueues values, while other ranks dequeue values.
 */
int main(int argc, char *argv[]) {
    int err = MPI_Init(&argc, &argv);
    if (err != MPI_SUCCESS) {
        fprintf(stderr, "MPI initialization failed\n");
        return 1;
    }

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (size < 2) {
        fprintf(stderr, "At least two processes are required\n");
        MPI_Finalize();
        return 1;
    }

    // Shared data structures
    int *head = NULL, *items = NULL, row = 0;
    MPI_Win win_head, win_items, win_row;

    // Initialize queue
    err = queue_initialize(rank, size, &head, &items, &row, &win_head, &win_items, &win_row);
    if (err != MPI_SUCCESS) {
        fprintf(stderr, "Queue initialization failed at rank %d\n", rank);
        MPI_Finalize();
        return 1;
    }

    // Enqueuer (rank 0) enqueues values 1, 2, 3
    int eng_row = 0, tail = 0;
    if (rank == 0) {
        int values[] = {1, 2, 3};
        for (int i = 0; i < 3; i++) {
            err = enqueue(values[i], rank, head, items, &row, win_head, win_items, win_row, &eng_row, &tail);
            if (err != MPI_SUCCESS) {
                fprintf(stderr, "Enqueue failed for value %d at rank %d\n", values[i], rank);
            } else {
                printf("Rank %d enqueued: %d\n", rank, values[i]);
            }
        }
    }

    // Barrier to ensure enqueues are done
    MPI_Barrier(MPI_COMM_WORLD);

    // Dequeuers try to dequeue
    int result = dequeue(rank, head, items, &row, win_head, win_items, win_row);
    if (result != -1) {
        printf("Rank %d dequeued: %d\n", rank, result);
    } else {
        printf("Rank %d found empty queue\n", rank);
    }

    // Clean up
    queue_finalize(rank, head, items, win_head, win_items, win_row);
    MPI_Finalize();
    return 0;
}