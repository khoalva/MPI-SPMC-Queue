#ifndef QUEUE_H
#define QUEUE_H

#include <mpi.h>

// Constants for special values
#define L -1 // ⊥ (empty cell)
#define T -2 // ⊤ (dequeued cell)
#define MAX_ROWS 1000 // Maximum rows in ITEMS
#define MAX_COLS 1000 // Maximum columns in ITEMS
#define MAX_VALUE 1000 // Maximum value to enqueue

// Function prototypes
int queue_initialize(int rank, int size, int **head, int **items, int *row, MPI_Win *win_head, MPI_Win *win_items, MPI_Win *win_row);
void queue_finalize(int rank, int *head, int *items, MPI_Win win_head, MPI_Win win_items, MPI_Win win_row);
int enqueue(int x, int rank, int *head, int *items, int *row, MPI_Win win_head, MPI_Win win_items, MPI_Win win_row, int *eng_row, int *tail);
int dequeue(int rank, int *head, int *items, int *row, MPI_Win win_head, MPI_Win win_items, MPI_Win win_row);

#endif // QUEUE_H