#include "queue.h"
#include <stdio.h>
#include <stdlib.h>

// Initialize shared data structures and MPI windows
int queue_initialize(int rank, int size, int **head, int **items, int *row, MPI_Win *win_head, MPI_Win *win_items, MPI_Win *win_row) {
    if (rank == 0) {
        *head = (int *)calloc(MAX_ROWS, sizeof(int));
        *items = (int *)calloc(MAX_ROWS * MAX_COLS, sizeof(int));
        *row = 0;
        if (!*head || !*items) {
            fprintf(stderr, "Memory allocation failed\n");
            return -1;
        }
        for (int i = 0; i < MAX_ROWS * MAX_COLS; i++) {
            (*items)[i] = L; // Initialize all cells to ⊥
        }
    }

    // Create MPI windows
    int err = MPI_Win_create(*head, rank == 0 ? MAX_ROWS * sizeof(int) : 0, sizeof(int), MPI_INFO_NULL, MPI_COMM_WORLD, win_head);
    if (err != MPI_SUCCESS) return err;
    err = MPI_Win_create(*items, rank == 0 ? MAX_ROWS * MAX_COLS * sizeof(int) : 0, sizeof(int), MPI_INFO_NULL, MPI_COMM_WORLD, win_items);
    if (err != MPI_SUCCESS) return err;
    err = MPI_Win_create(row, rank == 0 ? sizeof(int) : 0, sizeof(int), MPI_INFO_NULL, MPI_COMM_WORLD, win_row);
    if (err != MPI_SUCCESS) return err;

    // Start passive target synchronization
    err = MPI_Win_lock_all(0, *win_head);
    if (err != MPI_SUCCESS) return err;
    err = MPI_Win_lock_all(0, *win_items);
    if (err != MPI_SUCCESS) return err;
    err = MPI_Win_lock_all(0, *win_row);
    if (err != MPI_SUCCESS) return err;

    return MPI_SUCCESS;
}

// Finalize MPI windows and free memory
void queue_finalize(int rank, int *head, int *items, MPI_Win win_head, MPI_Win win_items, MPI_Win win_row) {
    // End passive target synchronization
    MPI_Win_unlock_all(win_head);
    MPI_Win_unlock_all(win_items);
    MPI_Win_unlock_all(win_row);

    // Free windows and memory
    MPI_Win_free(&win_head);
    MPI_Win_free(&win_items);
    MPI_Win_free(&win_row);
    if (rank == 0) {
        if (head) free(head);
        if (items) free(items);
    }
}

// Enqueue operation
int enqueue(int x, int rank, int *head, int *items, int *row, MPI_Win win_head, MPI_Win win_items, MPI_Win win_row, int *eng_row, int *tail) {
    if (rank != 0) return -1; // Only rank 0 is the enqueuer
    if (x < 0 || x > MAX_VALUE) {
        fprintf(stderr, "Invalid enqueue value: %d\n", x);
        return -1;
    }

    int val;
    int l_value = L; // Variable to hold L (⊥)
    // Step 1: Swap ITEMS[eng_row, tail] with x
    int err = MPI_Compare_and_swap(&x, &l_value, &val, MPI_INT, 0, (*eng_row * MAX_COLS + *tail), win_items);
    if (err != MPI_SUCCESS) return err;
    MPI_Win_flush(0, win_items);

    if (val == T) {
        // Dequeuer accessed this cell; move to next row
        (*eng_row)++;
        *tail = 0;
        if (*eng_row >= MAX_ROWS) {
            fprintf(stderr, "Row limit exceeded\n");
            return -1;
        }
        // Update ROW to new row
        err = MPI_Put(eng_row, 1, MPI_INT, 0, 0, 1, MPI_INT, win_row);
        if (err != MPI_SUCCESS) return err;
        MPI_Win_flush(0, win_row);
        // Swap ITEMS[eng_row, tail] with x (should return L)
        err = MPI_Compare_and_swap(&x, &l_value, &val, MPI_INT, 0, (*eng_row * MAX_COLS + *tail), win_items);
        if (err != MPI_SUCCESS) return err;
        MPI_Win_flush(0, win_items);
    }
    (*tail)++;
    return MPI_SUCCESS;
}

// Dequeue operation
int dequeue(int rank, int *head, int *items, int *row, MPI_Win win_head, MPI_Win win_items, MPI_Win win_row) {
    int deq_row, head_val, val;
    int l_value = L; // Variable to hold L (⊥)
    int t_value = T; // Variable to hold T (⊤)
    // Step 1: Read ROW
    int err = MPI_Get(&deq_row, 1, MPI_INT, 0, 0, 1, MPI_INT, win_row);
    if (err != MPI_SUCCESS) return -1;
    MPI_Win_flush(0, win_row);

    // Step 2: Fetch&Add HEAD[deq_row]
    int one = 1;
    err = MPI_Fetch_and_op(&one, &head_val, MPI_INT, 0, deq_row, MPI_SUM, win_head);
    if (err != MPI_SUCCESS) return -1;
    MPI_Win_flush(0, win_head);

    // Step 3: Swap ITEMS[deq_row, head] with T
    err = MPI_Compare_and_swap(&t_value, &l_value, &val, MPI_INT, 0, (deq_row * MAX_COLS + head_val), win_items);
    if (err != MPI_SUCCESS) return -1;
    MPI_Win_flush(0, win_items);

    // Return value or -1 if queue is empty
    return (val != T && val != L) ? val : -1;
}