#define _POSIX_C_SOURCE 199309L
#define _GNU_SOURCE  // Add this for broader usleep support
#include "spmc_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/**
 * @brief Initializes the SPMC queue and necessary MPI resources.
 */
int spmc_queue_init(spmc_queue_t *queue, int argc, char *argv[]) {
    if (!queue) return -1;

    MPI_TRY(mpi_init(argc, argv, &queue->mpi_ctx));

    if (mpi_get_size(&queue->mpi_ctx) < 2) {
        fprintf(stderr, "At least two processes (1 producer, 1+ consumer) are required.\n");
        mpi_finalize();
        return -1;
    }

    queue->row_size = MAX_ROW_SIZE;
    queue->num_row = MAX_NUM_ROWS;
    int is_producer = (mpi_get_rank(&queue->mpi_ctx) == 0);

    // Only the producer (rank 0) allocates and initializes the queue memory.
    if (is_producer) {
        queue->heads = malloc(queue->num_row * sizeof(int));
        queue->row_sealed = malloc(queue->num_row * sizeof(bool));
        
        // Allocate contiguous memory for all cells
        size_t total_cells = queue->num_row * queue->row_size;
        spmc_cell_t *all_cells = malloc(total_cells * sizeof(spmc_cell_t));
        
        // Allocate array of pointers to rows
        queue->cells = malloc(queue->num_row * sizeof(spmc_cell_t*));
        
        // Set up pointers to each row in the contiguous memory
        for (int i = 0; i < queue->num_row; i++) {
            queue->cells[i] = &all_cells[i * queue->row_size];
        }

        if (!all_cells || !queue->cells || !queue->heads || !queue->row_sealed) {
            fprintf(stderr, "Failed to allocate memory for queue cells.\n");
            mpi_finalize();
            return -1;
        }
        queue->tail = 0;
        for (int i = 0; i < queue->num_row; i++) {
            for (int j = 0; j < queue->row_size; j++) {
                queue->cells[i][j].rank = EMPTY_CELL;
                queue->cells[i][j].gap = 0;
                queue->cells[i][j].data = 0;
            }
        }
        for(int i = 0; i < queue->num_row; i++) {
            queue->heads[i] = 0;
            queue->row_sealed[i] = false;
        }
        queue->try_count = 0;
        queue->row = 0;
    } else {
        // Consumers do not allocate the main memory.
        queue->cells = NULL;
        queue->heads = NULL;
        queue->row_sealed = NULL;
        queue->tail = 0;
        queue->row = 0;
        queue->try_count = 0;
    }

    // Create MPI windows for one-sided access. The size is 0 for consumers.
    size_t cells_size = is_producer ? queue->row_size * queue->num_row * sizeof(spmc_cell_t) : 0;
    size_t head_size = is_producer ? queue->num_row * sizeof(int) : 0;
    size_t row_sealed_size = is_producer ? queue->num_row * sizeof(bool) : 0;
    
    // Add null pointer checks before creating windows
    void *cells_ptr = is_producer ? (void*)queue->cells[0] : NULL;
    void *heads_ptr = is_producer ? (void*)queue->heads : NULL;
    void *row_sealed_ptr = is_producer ? (void*)queue->row_sealed : NULL;
    MPI_TRY(mpi_win_create(cells_ptr, cells_size, 1, queue->mpi_ctx.comm, &queue->win_cells));
    MPI_TRY(mpi_win_create(heads_ptr, head_size, sizeof(int), queue->mpi_ctx.comm, &queue->win_heads));
    MPI_TRY(mpi_win_create(row_sealed_ptr, row_sealed_size, sizeof(bool), queue->mpi_ctx.comm, &queue->win_row_sealed));

    // Lock all windows to enable passive target synchronization, which allows
    // consumers to perform RMA operations without explicit calls from the producer.
    mpi_window_t windows[] = {queue->win_cells, queue->win_heads, queue->win_row_sealed};
    MPI_TRY(mpi_win_lock_all_multiple(windows, 3));

    printf("SPMC Queue initialized on rank %d/%d\n",
           mpi_get_rank(&queue->mpi_ctx), mpi_get_size(&queue->mpi_ctx));

    return MPI_SUCCESS;
}

/**
 * @brief Destroys the queue and frees all associated resources.
 */
void spmc_queue_destroy(spmc_queue_t *queue) {
    if (!queue) return;

    mpi_window_t windows[] = {queue->win_cells, queue->win_heads, queue->win_row_sealed};
    mpi_win_unlock_all_multiple(windows, 3);

    mpi_win_destroy(&queue->win_cells);
    mpi_win_destroy(&queue->win_heads);
    mpi_win_destroy(&queue->win_row_sealed);
    // The producer frees the memory it allocated.
    if (spmc_queue_is_enqueuer(queue) && queue->cells) {
        // Free the contiguous memory block (pointed to by queue->cells[0])
        free(queue->cells[0]);
        // Free the array of pointers
        free(queue->cells);
        queue->cells = NULL;
    }
    free(queue->heads);
    free(queue->row_sealed);
    printf("SPMC Queue destroyed on rank %d\n", mpi_get_rank(&queue->mpi_ctx));
    mpi_finalize();
}

/**
 * @brief Enqueues an item using FFQ logic. Only the producer (rank 0) should call this.
 * @return MPI_SUCCESS on success, or -1 if the queue is full or contended.
 */
int spmc_queue_enqueue(spmc_queue_t *queue, int value) {
    if (!spmc_queue_is_enqueuer(queue)) return -1;
    bool success = false;
    while(!success){
        
        int current_tail_val = queue->tail;
        // int head_val;

        // // First, do a robust check for fullness to prevent tail from lapping head indefinitely.
        // MPI_TRY(mpi_get(&head_val, sizeof(int), MPI_BYTE, 0, queue->row * sizeof(int), &queue->win_heads));

        // if ((current_tail_val - head_val) >= queue->row_size) {
        //     printf("[ENQUEUE][rank %d] Queue is full! tail=%d, head=%d, row=%d\n",
        //          mpi_get_rank(&queue->mpi_ctx), current_tail_val, head_val, queue->row);
        //     return -1;
        // }

        int pos = current_tail_val % queue->row_size;
        
        // Add bounds checking
        if (queue->row >= queue->num_row || pos >= queue->row_size) {
            printf("[ENQUEUE][rank %d] Invalid position: row=%d, pos=%d\n", 
                mpi_get_rank(&queue->mpi_ctx), queue->row, pos);
            return -1;
        }
        
        spmc_cell_t cell;

        // Read the current state of the target cell from the MPI window.
        MPI_Aint cell_offset = (queue->row * queue->row_size + pos) * sizeof(spmc_cell_t);
        MPI_TRY(mpi_get(&cell, sizeof(spmc_cell_t), MPI_BYTE, 0, cell_offset, &queue->win_cells));

        // Check if the cell is used (rank >= 0).
        if (cell.rank != EMPTY_CELL && queue->try_count < MAX_TRY_COUNT) {
            // The cell is currently occupied by a previous value that a consumer hasn't processed yet.
            // As per the FFQ algorithm, mark a "gap" to indicate this slot was skipped.
            printf("[ENQUEUE][rank %d] Contention at pos %d. Cell is not empty.\n", mpi_get_rank(&queue->mpi_ctx), pos);
            cell.gap = current_tail_val;
            MPI_TRY(mpi_put(&cell, sizeof(spmc_cell_t), MPI_BYTE, 0, cell_offset, &queue->win_cells));
            queue->try_count++;
            queue->tail++;
        } else if(queue->try_count >= MAX_TRY_COUNT){
            // If we have tried too many times, we can consider sealing the current row and moving to the next.
            queue->row_sealed[queue->row] = true;
            MPI_TRY(mpi_put(&queue->row_sealed[queue->row], sizeof(bool), MPI_BYTE, 0, queue->row * sizeof(bool), &queue->win_row_sealed));
            queue->row++;
            queue->try_count = 0;
            queue->tail = 0;
            if(queue->row >= queue->num_row){
                printf("[ENQUEUE][rank %d] All rows are sealed. Queue is full!\n", mpi_get_rank(&queue->mpi_ctx));
                return -1; // Queue is full
            }
            printf("[ENQUEUE][rank %d] Moving to next row: %d\n", mpi_get_rank(&queue->mpi_ctx), queue->row);
        } else {
            // The cell is empty, so we can claim it.
            cell.data = value;
            cell.rank = current_tail_val; // Claim the cell by setting its rank to the current tail value.
            cell.gap = 0; // Reset gap.

            // Put the new cell data into shared memory.
            MPI_TRY(mpi_put(&cell, sizeof(spmc_cell_t), MPI_BYTE, 0, cell_offset, &queue->win_cells));

            // IMPORTANT: The local tail is only incremented after the data is successfully written.
            queue->tail = current_tail_val + 1;

            printf("[ENQUEUE][rank %d] Enqueued item: %d at pos %d | new_tail=%d\n",
                mpi_get_rank(&queue->mpi_ctx), value, pos, queue->tail);

            success = true;
        }
    }
    return MPI_SUCCESS;
}
/**
 * @brief Dequeues an item using FFQ logic. This function will not loop indefinitely.
 * @return MPI_SUCCESS on success, -1 on failure (e.g., queue empty, contention, or timeout).
 */
int spmc_queue_dequeue(spmc_queue_t *queue) {
    if (spmc_queue_is_enqueuer(queue)) return -1;

    int my_rank;
    int one = 1;
    const int MAX_POLL_ATTEMPTS = 200; // Poll for ~2ms before giving up.

    // Atomically get a rank to process. This is our one "ticket" for this attempt.
    MPI_TRY(mpi_fetch_and_op(&one, &my_rank, MPI_INT, 0, queue->row * sizeof(int), MPI_SUM, &queue->win_heads));

    int pos = my_rank % queue->row_size;
    
    // Add bounds checking
    if (queue->row >= queue->num_row || pos >= queue->row_size) {
        printf("[DEQUEUE][rank %d] Invalid position: row=%d, pos=%d\n", 
               mpi_get_rank(&queue->mpi_ctx), queue->row, pos);
        return -1;
    }
    
    MPI_Aint disp = (queue->row * queue->row_size + pos) * sizeof(spmc_cell_t);
    int poll_attempts = 0;

    // Poll the specific cell for a limited time.
    while (poll_attempts < MAX_POLL_ATTEMPTS) {
        spmc_cell_t c;
        MPI_TRY(mpi_get(&c, sizeof(spmc_cell_t), MPI_BYTE, 0, disp, &queue->win_cells));

        // Case 1: The cell's rank matches our rank. It's ready for us to claim.
        if (c.rank == my_rank) {
            // Attempt to claim the cell via Compare-and-Swap.
            int empty_val = EMPTY_CELL;
            int compare_val = my_rank;
            int result_val;
            MPI_Aint rank_disp = disp + offsetof(spmc_cell_t, rank);

            // Assuming mpi_compare_and_swap exists and maps to MPI_Compare_and_swap.
            MPI_TRY(mpi_compare_and_swap(&empty_val, &compare_val, &result_val, MPI_INT, 0, rank_disp, &queue->win_cells));

            // If the value before the swap was our rank, we successfully claimed it.
            if (result_val == my_rank) {
                printf("[DEQUEUE][rank %d] Dequeued item: %d at pos %d\n", mpi_get_rank(&queue->mpi_ctx), c.data, pos);
                return MPI_SUCCESS;
            } else {
                // We lost a race to another consumer for this specific item.
                return -1;
            }
        }
        // Case 2: The cell has been skipped by the producer (a "gap").
        else if (c.gap >= my_rank && c.rank != my_rank) {
            return -1;
        }
        // Case 3: The cell is not ready yet. Wait and poll again.
        else {
            bool is_row_sealed;
            MPI_TRY(mpi_get(&is_row_sealed, sizeof(bool), MPI_BYTE, 0, queue->row * sizeof(bool), &queue->win_row_sealed));
            if (is_row_sealed) {
                // The row is sealed, try next row.
                queue->row++;
                
            }
            else{
                usleep(10); // Back off to prevent busy-spinning.
                poll_attempts++;
            }
        }
    }

    // If we exit the loop, it means we timed out waiting for the producer. The queue is likely empty.
    return -1;
}

/**
 * @brief Prints statistics about the queue.
 */
void spmc_queue_print_stats(spmc_queue_t *queue) {
    if (spmc_queue_is_enqueuer(queue)) {
        printf("Queue Stats -> size: %d, head: %d, tail: %d\n", queue->row_size, queue->heads[queue->row], queue->tail);
    }
}

/**
 * @brief Checks if the current process is the producer (rank 0).
 */
int spmc_queue_is_enqueuer(spmc_queue_t *queue) {
    return queue && mpi_is_root(&queue->mpi_ctx);
}

/**
 * @brief Returns the total bytes allocated by the queue.
 */
size_t spmc_queue_get_capacity_bytes(spmc_queue_t *queue) {
    if (!queue) return 0;
    if (spmc_queue_is_enqueuer(queue)) {
        return queue->num_row * queue->row_size * sizeof(spmc_cell_t);
    }
    return 0;
}