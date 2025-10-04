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
        queue->row_epochs = malloc(queue->num_row * sizeof(int));
        // Allocate contiguous memory for all cells
        size_t total_cells = queue->num_row * queue->row_size;
        spmc_cell_t *all_cells = malloc(total_cells * sizeof(spmc_cell_t));
        
        // Allocate array of pointers to rows
        queue->cells = malloc(queue->num_row * sizeof(spmc_cell_t*));
        
        // Set up pointers to each row in the contiguous memory
        for (int i = 0; i < queue->num_row; i++) {
            queue->cells[i] = &all_cells[i * queue->row_size];
        }

        if (!all_cells || !queue->cells || !queue->heads || !queue->row_epochs) {
            fprintf(stderr, "Failed to allocate memory for queue cells.\n");
            mpi_finalize();
            return -1;
        }
        queue->tail = 0;
        for (int i = 0; i < queue->num_row; i++) {
            for (int j = 0; j < queue->row_size; j++) {
                queue->cells[i][j].rank = EMPTY_CELL;
                queue->cells[i][j].gap = -1;
                queue->cells[i][j].data = 0;
                queue->cells[i][j].row_epoch = 0;
            }
        }
        
        for(int i = 0; i < queue->num_row; i++) {
            queue->heads[i] = 0;
            queue->row_epochs[i] = -1;  // Khởi tạo tất cả row_epochs = -1
        }
        queue->row_epochs[0] = 0; // Row 0 bắt đầu với epoch 0
        queue->try_count = 0;
        queue->row = 0;
        queue->row_epoch = 0;

        queue->producer_log = malloc(sizeof(BitLog_t));
        queue->consumer_log = malloc(sizeof(BitLog_t));
        bitlog_init(queue->producer_log, MAX_NUM_ROWS, MAX_LOG_SIZE);
        bitlog_init(queue->consumer_log, MAX_NUM_ROWS, MAX_LOG_SIZE);
    } else {
        // Consumers do not allocate the main memory.
        queue->cells = NULL;
        queue->heads = NULL;
        queue->tail = 0;
        queue->row = 0;
        queue->try_count = 0;
        queue->row_epoch = 0;
        queue->row_epochs = NULL;
        queue->producer_log = NULL;
        queue->consumer_log = NULL;
    }

    // Create MPI windows for one-sided access. The size is 0 for consumers.
    size_t cells_size = is_producer ? queue->row_size * queue->num_row * sizeof(spmc_cell_t) : 0;
    size_t head_size = is_producer ? queue->num_row * sizeof(int) : 0;
    size_t bitlog_size = is_producer ? (MPI_Aint)queue->producer_log->rows * queue->producer_log->words_per_row * sizeof(uint64_t) : 0;
    // Add null pointer checks before creating windows
    void *cells_ptr = is_producer ? (void*)queue->cells[0] : NULL;
    void *heads_ptr = is_producer ? (void*)queue->heads : NULL;
    void *row_epochs_ptr = is_producer ? (void*)queue->row_epochs : NULL;
    void *producer_log_ptr = is_producer ? (void*)queue->producer_log->data : NULL;
    void *consumer_log_ptr = is_producer ? (void*)queue->consumer_log->data : NULL;

    MPI_TRY(mpi_win_create(cells_ptr, cells_size, 1, queue->mpi_ctx.comm, &queue->win_cells));
    MPI_TRY(mpi_win_create(heads_ptr, head_size, sizeof(int), queue->mpi_ctx.comm, &queue->win_heads));
    MPI_TRY(mpi_win_create(row_epochs_ptr, head_size, sizeof(int), queue->mpi_ctx.comm, &queue->win_row_epochs));
    MPI_TRY(mpi_win_create(producer_log_ptr, bitlog_size, sizeof(uint64_t), queue->mpi_ctx.comm, &queue->win_producer_log));
    MPI_TRY(mpi_win_create(consumer_log_ptr, bitlog_size, sizeof(uint64_t), queue->mpi_ctx.comm, &queue->win_consumer_log));

    // Lock all windows to enable passive target synchronization, which allows
    // consumers to perform RMA operations without explicit calls from the producer.
    mpi_window_t windows[] = {queue->win_cells, queue->win_heads, queue->win_row_epochs,
                              queue->win_producer_log, queue->win_consumer_log};
    MPI_TRY(mpi_win_lock_all_multiple(windows, 5));

    printf("SPMC Queue initialized on rank %d/%d\n",
           mpi_get_rank(&queue->mpi_ctx), mpi_get_size(&queue->mpi_ctx));

    return MPI_SUCCESS;
}

/**
 * @brief Destroys the queue and frees all associated resources.
 */
void spmc_queue_destroy(spmc_queue_t *queue) {
    if (!queue) return;

    mpi_window_t windows[] = {queue->win_cells, queue->win_heads, queue->win_row_epochs,
                              queue->win_producer_log, queue->win_consumer_log};
    mpi_win_unlock_all_multiple(windows, 5);

    mpi_win_destroy(&queue->win_cells);
    mpi_win_destroy(&queue->win_heads);
    mpi_win_destroy(&queue->win_row_epochs);
    mpi_win_destroy(&queue->win_producer_log);
    mpi_win_destroy(&queue->win_consumer_log);
    // The producer frees the memory it allocated.
    if (spmc_queue_is_enqueuer(queue) && queue->cells) {
        // Free the contiguous memory block (pointed to by queue->cells[0])
        free(queue->cells[0]);
        // Free the array of pointers
        free(queue->cells);
        queue->cells = NULL;
    }
    if (queue->heads) {
        free(queue->heads);
    }
    if (queue->row_epochs) {
        free(queue->row_epochs);
    }
    if (queue->producer_log) {
        bitlog_destroy(queue->producer_log);
    }
    if (queue->consumer_log) {
        bitlog_destroy(queue->consumer_log);
    }
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
        int row = queue->row % MAX_NUM_ROWS;
        spmc_cell_t cell;

        // Read the current state of the target cell from the MPI window.
        MPI_Aint cell_offset = ((row * queue->row_size + pos) * sizeof(spmc_cell_t));
        MPI_TRY(mpi_get(&cell, sizeof(spmc_cell_t), MPI_BYTE, 0, cell_offset, &queue->win_cells));

        // Check if the cell is used (rank >= 0).
        if (cell.rank != EMPTY_CELL && queue->try_count < MAX_TRY_COUNT) {
            // The cell is currently occupied by a previous value that a consumer hasn't processed yet.
            // As per the FFQ algorithm, mark a "gap" to indicate this slot was skipped.
            printf("[ENQUEUE][rank %d] Contention at pos %d. Cell is not empty.\n", mpi_get_rank(&queue->mpi_ctx), pos);
            cell.gap = current_tail_val;
            cell.row_epoch = queue->row_epoch;
            MPI_TRY(mpi_put(&cell, sizeof(spmc_cell_t), MPI_BYTE, 0, cell_offset, &queue->win_cells));
            queue->try_count++;
            queue->tail++;
        } else if(queue->try_count >= MAX_TRY_COUNT){
            printf("[ENQUEUE][rank %d] Max tries reached at pos %d. Sealing row %d.\n", mpi_get_rank(&queue->mpi_ctx), pos, queue->row);
            fflush(stdout);  // Flush buffer trước khi tiếp tục
            
            // If we have tried too many times, we can consider sealing the current row and moving to the next.
            int word_idx = queue->row_epoch / 64;
            int bit_pos  = queue->row_epoch % 64;
            uint64_t mask = 1ULL << bit_pos;
            MPI_Aint disp = (MPI_Aint)(row * queue->producer_log->words_per_row + word_idx) * sizeof(uint64_t);
            MPI_TRY(mpi_accumulate(&mask, 1, MPI_UINT64_T, 0, disp, MPI_BOR, &queue->win_producer_log));
            
            printf("Update producer log at row %d (row_epoch=%d)\n", queue->row, queue->row_epoch);
            queue->row = (queue->row + 1) % MAX_NUM_ROWS;

            int one = 1;
            MPI_TRY(mpi_fetch_and_op(&one, &queue->row_epoch, MPI_INT, 0, (MPI_Aint) (queue->row * sizeof(int)), MPI_SUM, &queue->win_row_epochs));
            
            queue->row_epoch++;
            printf("[ENQUEUE][rank %d] Got row_epoch for new row %d: %d\n", 
                   mpi_get_rank(&queue->mpi_ctx), queue->row, queue->row_epoch);
            fflush(stdout);
            
            queue->try_count = 0;
            queue->tail = 0;
            
            printf("[ENQUEUE][rank %d] Starting to wait for consumer signal for row_epoch %d\n", 
                   mpi_get_rank(&queue->mpi_ctx), queue->row_epoch);
            fflush(stdout);
            
            int cnt = 0;
            while(cnt < 10){
                if(queue->row_epoch == 0) break;
                uint64_t* bl = malloc(queue->consumer_log->words_per_row * sizeof(uint64_t));
                
                MPI_TRY(mpi_get(bl, 
                queue->consumer_log->words_per_row, MPI_UINT64_T, 
                0, 
                (MPI_Aint) queue->row* queue->consumer_log->words_per_row * sizeof(uint64_t), 
                &queue->win_consumer_log));
                
                int bit_value = check_bit(bl, queue->row_epoch);
                
                if (bit_value == 1) {
                    free(bl);
                    break;
                }
                free(bl);
                
                sleep(0.1);  // 1 giây
                cnt++;
            }
            if(cnt == 10){
                printf("[ENQUEUE][rank %d] Consumer is too slow! Exiting enqueue.\n", mpi_get_rank(&queue->mpi_ctx));
                fflush(stdout);
                return -1; // Return immediately, don't continue the while loop
            }

            printf("[ENQUEUE][rank %d] Moving to next row: %d, row_epoch: %d\n", 
                   mpi_get_rank(&queue->mpi_ctx), queue->row % MAX_NUM_ROWS, queue->row_epoch);
        } else {
            // The cell is empty, so we can claim it.
            cell.data = value;
            cell.rank = current_tail_val; // Claim the cell by setting its rank to the current tail value.
            cell.row_epoch = queue->row_epoch;
            // Put the new cell data into shared memory.
            MPI_TRY(mpi_put(&cell, sizeof(spmc_cell_t), MPI_BYTE, 0, cell_offset, &queue->win_cells));

            // IMPORTANT: The local tail is only incremented after the data is successfully written.
            queue->tail = current_tail_val + 1;

            printf("[ENQUEUE][rank %d] Enqueued item: %d at pos %d row %d | new_tail=%d\n",
                mpi_get_rank(&queue->mpi_ctx), value, pos, queue->row, queue->tail);

            success = true;
        }
    }
    return MPI_SUCCESS;
}
/**
 * @brief Dequeues an item using FFQ logic. This function will not loop indefinitely.
 * @return The dequeued data value on success, -1 on failure (e.g., queue empty, contention, or timeout).
 */
int spmc_queue_dequeue(spmc_queue_t *queue) {
    if (spmc_queue_is_enqueuer(queue)) return -1;

    bool success = false;
    int dequeued_data = -1;  // Store the dequeued data value
    // Poll the specific cell for a limited time.
    int my_rank;
    int one = 1;
    // Atomically get a rank to process. This is our one "ticket" for this attempt.
    MPI_TRY(mpi_fetch_and_op(&one, &my_rank, MPI_INT, 0, queue->row * sizeof(int), MPI_SUM, &queue->win_heads));

    int pos = my_rank % queue->row_size;
    MPI_Aint disp = (queue->row * queue->row_size + pos) * sizeof(spmc_cell_t);

    spmc_cell_t c;
    MPI_TRY(mpi_get(&c, sizeof(spmc_cell_t), MPI_BYTE, 0, disp, &queue->win_cells));
    while (!success) {
        // Case 1: The cell's rank matches our rank. It's ready for us to claim.
        if (c.rank == my_rank) {
            // Attempt to claim the cell via Compare-and-Swap.
            int empty_val = EMPTY_CELL;
            int compare_val = my_rank;
            int result_val;
            MPI_Aint rank_disp = disp + offsetof(spmc_cell_t, rank);

            MPI_TRY(mpi_compare_and_swap(&empty_val, &compare_val, &result_val, MPI_INT, 0, rank_disp, &queue->win_cells));

            dequeued_data = c.data;  // Store the data value
            printf("[DEQUEUE][rank %d] Dequeued item: %d at pos %d row %d\n", mpi_get_rank(&queue->mpi_ctx), c.data, pos, queue->row);
            success = true;

        }
        // Case 2: The cell has been skipped by the producer (a "gap").
        else if (((c.gap >= my_rank) && (c.rank != my_rank)) && (c.row_epoch == queue->row_epoch)) {
            printf("[DEQUEUE][rank %d] Cell has been skipped (gap: %d, cell_rank: %d, consumer_rank: %d, cell_row_epoch: %d, my_row_epoch: %d, row: %d)\n",
                   mpi_get_rank(&queue->mpi_ctx), c.gap, c.rank, my_rank, c.row_epoch, queue->row_epoch, queue->row);
            MPI_TRY(mpi_fetch_and_op(&one, &my_rank, MPI_INT, 0, queue->row * sizeof(int), MPI_SUM, &queue->win_heads));

            int pos = my_rank % queue->row_size;
            MPI_Aint disp = (queue->row * queue->row_size + pos) * sizeof(spmc_cell_t);

            MPI_TRY(mpi_get(&c, sizeof(spmc_cell_t), MPI_BYTE, 0, disp, &queue->win_cells));
        }
        // Case 3: The cell is not ready yet. Wait and poll again.
        else {
            printf("[DEQUEUE][rank %d] Cell is not ready yet. Waiting...\n", mpi_get_rank(&queue->mpi_ctx));
            // Get the words_per_row value - we need to calculate it since consumer doesn't have BitLog allocated
            int words_per_row = (MAX_LOG_SIZE + 63) / 64;  // Same calculation as in bitlog_init
            int cnt = 1;
            while(cnt > 0){
                uint64_t* bl = malloc(sizeof(uint64_t) * words_per_row);
                MPI_TRY(mpi_get(bl, words_per_row, MPI_UINT64_T, 0, (MPI_Aint) queue->row * words_per_row * sizeof(uint64_t), &queue->win_producer_log));
                
                // Check bit BEFORE freeing the memory
                int bit_set = check_bit(bl, queue->row_epoch);
                free(bl);
                
                if(bit_set == 1){
                    printf("[DEQUEUE][rank %d] Bit is set (row_epoch: %d) - Moving to next row\n", mpi_get_rank(&queue->mpi_ctx), queue->row_epoch);
                    int word_idx = queue->row_epoch / 64;
                    int bit_pos  = queue->row_epoch % 64;
                    uint64_t mask = 1ULL << bit_pos;
                    MPI_Aint disp = (MPI_Aint)(queue->row * words_per_row + word_idx) * sizeof(uint64_t);
                    MPI_TRY(mpi_accumulate(&mask, 1, MPI_UINT64_T, 0, disp, MPI_BOR, &queue->win_consumer_log));
                    int zero = 0;
                    MPI_TRY(mpi_accumulate(&zero, 1, MPI_INT, 0, queue->row * sizeof(int), MPI_REPLACE, &queue->win_heads));
                    int old_row = queue->row;
                    queue->row = (queue->row + 1) % queue->num_row;
                    MPI_TRY(mpi_get(&queue->row_epoch, 1, MPI_INT, 0, (MPI_Aint) queue->row * sizeof(int), &queue->win_row_epochs));
                    printf("[DEQUEUE][rank %d] Moved from row %d to row %d, new row_epoch: %d\n", 
                           mpi_get_rank(&queue->mpi_ctx), old_row, queue->row, queue->row_epoch);
                    break;
                } else{
                    printf("[DEQUEUE][rank %d] Bit is not set (row_epoch: %d)\n", mpi_get_rank(&queue->mpi_ctx), queue->row_epoch);
                    usleep(10); // sleep in 10 microseconds
                    cnt--;
                }
            }
            if(cnt == 0){
                fprintf(stderr, "[DEQUEUE][rank %d] Consumer is too slow or producer hasn't produced! Exiting dequeue.\n", mpi_get_rank(&queue->mpi_ctx));
                return -1;
            }
            
            // After moving to new row, get a new rank and check for items
            printf("[DEQUEUE][rank %d] Moved to new row %d with row_epoch %d. Getting new rank...\n", 
                   mpi_get_rank(&queue->mpi_ctx), queue->row, queue->row_epoch);
            MPI_TRY(mpi_fetch_and_op(&one, &my_rank, MPI_INT, 0, queue->row * sizeof(int), MPI_SUM, &queue->win_heads));

            int pos = my_rank % queue->row_size;
            MPI_Aint disp = (queue->row * queue->row_size + pos) * sizeof(spmc_cell_t);

            MPI_TRY(mpi_get(&c, sizeof(spmc_cell_t), MPI_BYTE, 0, disp, &queue->win_cells));
            printf("[DEQUEUE][rank %d] New position %d, cell rank: %d, my_rank: %d, cell_row_epoch: %d, my_row_epoch: %d\n", 
                   mpi_get_rank(&queue->mpi_ctx), pos, c.rank, my_rank, c.row_epoch, queue->row_epoch);
        }
    }
    
    return success ? dequeued_data : -1;  // Return the actual data value or -1 on failure
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
    
    size_t total_bytes = 0;
    
    // Only the producer (rank 0) allocates the main memory structures
    if (spmc_queue_is_enqueuer(queue)) {
        // 1. Memory for cells (main data storage)
        total_bytes += queue->num_row * queue->row_size * sizeof(spmc_cell_t);
        
        // 2. Memory for heads array
        total_bytes += queue->num_row * sizeof(int);
        
        // 3. Memory for row_epochs array
        total_bytes += queue->num_row * sizeof(int);
        
        // 4. Memory for cells pointer array
        total_bytes += queue->num_row * sizeof(spmc_cell_t*);
        
        // 5. Memory for producer_log BitLog structure
        if (queue->producer_log) {
            total_bytes += sizeof(BitLog_t);
            total_bytes += queue->producer_log->rows * queue->producer_log->words_per_row * sizeof(uint64_t);
        }
        
        // 6. Memory for consumer_log BitLog structure
        if (queue->consumer_log) {
            total_bytes += sizeof(BitLog_t);
            total_bytes += queue->consumer_log->rows * queue->consumer_log->words_per_row * sizeof(uint64_t);
        }
    }
    
    // 7. Basic queue structure memory (allocated for all processes)
    total_bytes += sizeof(spmc_queue_t);
    
    return total_bytes;
}
/**
 * @brief Initializes a BitLog structure.
 */
void bitlog_init(BitLog_t* log, int rows, int cols) {
    log->rows = rows;
    log->cols = cols;
    log->words_per_row = (cols + 63) / 64;
    log->data = calloc(rows * log->words_per_row, sizeof(uint64_t));
}

/**
 * @brief Destroys a BitLog structure.
 */
void bitlog_destroy(BitLog_t* log) {
    if (log) {
        free(log->data);
        free(log);
    }
}
/**
 * @brief Return the last bit (0 or 1)
 */
int check_bit(uint64_t *row, int col) {
    int word = col / 64;
    int bit  = col % 64;
    return (row[word] >> bit) & 1ULL;
}