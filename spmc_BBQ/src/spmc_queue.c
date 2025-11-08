#include "spmc_queue.h"
#include "bitmap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

// Timer utilities
static inline double get_time_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000.0 + tv.tv_usec;
}

static inline void print_timing(const char* operation, double start_time, int rank) {
    double elapsed = get_time_us() - start_time;
    // printf("[TIMER][Rank %d] %s: %.2f μs\n", rank, operation, elapsed);
}


int spmc_queue_init_with_queue_owner(spmc_queue_t *queue, int argc, char *argv[], int queue_owner_rank){
    if (!queue) return -1;

    // Initialize MPI context
    MPI_TRY(mpi_init(argc, argv, &queue->mpi_ctx));

    if (mpi_get_size(&queue->mpi_ctx) < 2) {
        // fprintf(stderr, "At least two processes (1 producer, 1+ consumer) are required.\n");
        mpi_finalize();
        return -1;
    }

    int rank = mpi_get_rank(&queue->mpi_ctx);
    int size = mpi_get_size(&queue->mpi_ctx);
    
    // Validate queue owner rank
    if (queue_owner_rank < 0 || queue_owner_rank >= size) {
        if (rank == 0) {
            fprintf(stderr, "Invalid queue owner rank %d (must be 0-%d)\n", queue_owner_rank, size-1);
        }
        mpi_finalize();
        return -1;
    }
    
    // Store queue owner rank in queue structure
    queue->queue_owner_rank = queue_owner_rank;
    
    // Producer is always rank 0
    int is_producer = (rank == 0);
    
    // Queue owner is the one who allocates memory
    int is_queue_owner = (rank == queue_owner_rank);

    // Initialize pointers to NULL
    queue->p = NULL;
    queue->c = NULL;

    // Initialize Structure (main queue structure)
    queue->q = malloc(sizeof(structure_t));
    if (!queue->q) {
        // fprintf(stderr, "Failed to allocate memory for Structure.\n");
        mpi_finalize();
        return -1;
    }

    // For now, using placeholder values - need to implement proper sizing logic
    int rows = MAX_ROW;  
    int cols = MAX_QUEUE_SIZE;

    // Producer (rank 0) always has producer structure
    if (is_producer) {
        
           // Initialize Producer structure
        queue->p = malloc(sizeof(producer_t));
        if (!queue->p) {
            // fprintf(stderr, "Failed to allocate memory for Producer structure.\n");
            free(queue->q);
            mpi_finalize();
            return -1;
        }

        // Initialize producer-specific fields
        queue->p->tail = 0;
        queue->p->enq_row = 0;
        queue->p->size = MAX_QUEUE_SIZE;
        
        queue->p->map = malloc(sizeof(bitmap_t));
        bitmap_init(queue->p->map, 1, cols);
        set_all_bits_full(queue->p->map);
    }

    // Queue owner allocates the actual queue memory
    if (is_queue_owner) {

        // Initialize items array
        queue->q->items = malloc(MAX_QUEUE_SIZE * sizeof(cell_t));
        if (!queue->q->items) {
            // fprintf(stderr, "Failed to allocate memory for queue items.\n");
            // TODO: Add proper cleanup
            return -1;
        }

        // Initialize items with empty values
        for (int i = 0; i < MAX_QUEUE_SIZE; i++) {
            queue->q->items[i] = MAKE_CELL(L, 0);  // Empty cell
        }

        // Initialize heads array (one per consumer)
        int num_consumers = size - 1;
        queue->q->heads = malloc(num_consumers * sizeof(int));
        if (!queue->q->heads) {
            // fprintf(stderr, "Failed to allocate memory for heads array.\n");
            // TODO: Add proper cleanup
            return -1;
        }
        
        // Initialize all heads to 0
        for (int i = 0; i < num_consumers; i++) {
            queue->q->heads[i] = 0;
        }

        // Initialize bitmap structures
        queue->q->bitmap = malloc(sizeof(bitmap_t));
        queue->q->sync_bitmap = malloc(sizeof(bitmap_t));

        bitmap_init(queue->q->bitmap, rows, cols);
        bitmap_init(queue->q->sync_bitmap, rows, cols);

        // Initialize Structure fields
        queue->q->row = 0;
        // queue->q->rows = rows;
        // queue->q->cols = cols;

    } else {
        // Non-queue-owner nodes don't allocate main memory
        queue->q->items = NULL;
        queue->q->heads = NULL;
        queue->q->row = 0;
        // queue->q->rows = rows;
        // queue->q->cols = cols;
        
        // Consumer needs bitmap structures for window creation (but no data)
        queue->q->bitmap = malloc(sizeof(bitmap_t));
        queue->q->sync_bitmap = malloc(sizeof(bitmap_t));
        queue->q->bitmap->data = NULL;
        queue->q->sync_bitmap->data = NULL;
        queue->q->bitmap->rows = rows;
        queue->q->bitmap->cols = cols;
        queue->q->bitmap->words_per_row = (cols + 63) / 64;
        queue->q->sync_bitmap->rows = rows;
        queue->q->sync_bitmap->cols = cols;
        queue->q->sync_bitmap->words_per_row = (cols + 63) / 64;
    }
    
    // Consumers (all non-producers) need consumer structure
    if (!is_producer) {
        queue->c = malloc(sizeof(consumer_t));
        if (!queue->c) {
            // fprintf(stderr, "Failed to allocate memory for Consumer structure.\n");
            free(queue->q);
            mpi_finalize();
            return -1;
        }
        queue->c->size = MAX_QUEUE_SIZE;
        queue->c->last_value = T;
        queue->c->last_deq_row = 0;
        // Initialize optimization variables
        queue->c->last_index = -1;
        queue->c->last_N = -1;
        
        queue->c->map = malloc(sizeof(bitmap_t));
        bitmap_init(queue->c->map, 1, cols);
        set_all_bits_full(queue->c->map);
    }

    // Create MPI windows for one-sided access
    // Only queue owner allocates memory for windows
    size_t items_size = is_queue_owner ? MAX_QUEUE_SIZE * sizeof(cell_t) : 0;
    size_t heads_size = is_queue_owner ? (size - 1) * sizeof(int) : 0;
    size_t row_size = is_queue_owner ? sizeof(int) : 0;
    size_t bitmap_size = is_queue_owner ? (MPI_Aint)queue->q->bitmap->rows * queue->q->bitmap->words_per_row * sizeof(uint64_t) : 0;


    MPI_TRY(mpi_win_create(queue->q->items, items_size, sizeof(cell_t), 
                          queue->mpi_ctx.comm, &queue->q->win_items));
    MPI_TRY(mpi_win_create(queue->q->heads, heads_size, sizeof(int), 
                          queue->mpi_ctx.comm, &queue->q->win_heads));
    MPI_TRY(mpi_win_create(&queue->q->row, row_size, sizeof(int), 
                          queue->mpi_ctx.comm, &queue->q->win_row));
    MPI_TRY(mpi_win_create(queue->q->bitmap->data, bitmap_size, 1, 
                          queue->mpi_ctx.comm, &queue->q->win_bitmap));
    MPI_TRY(mpi_win_create(queue->q->sync_bitmap->data, bitmap_size, 1, 
                          queue->mpi_ctx.comm, &queue->q->win_sync_bitmap));
    
    // TODO: Add bitmap windows to locking when implemented
    mpi_window_t all_windows[] = {queue->q->win_items, queue->q->win_heads, 
                                 queue->q->win_row, queue->q->win_bitmap, 
                                 queue->q->win_sync_bitmap};
    MPI_TRY(mpi_win_lock_all_multiple(all_windows, 5));
    // print_queue_bitmaps(queue, 5, 5);
    // printf("GWMQ SPMC Queue initialized on rank %d/%d\n", rank, size);

    return MPI_SUCCESS;
}

// Backward compatibility: default queue owner at rank 0 (local operations)
int spmc_queue_init(spmc_queue_t *queue, int argc, char *argv[]) {
    return spmc_queue_init_with_queue_owner(queue, argc, argv, 0);
}


void spmc_queue_destroy(spmc_queue_t *queue) {
    if (!queue) return;
    // print_queue_bitmaps(queue, 5, 5);
    int rank = mpi_get_rank(&queue->mpi_ctx);
    // printf("[Rank %d] Starting queue destruction\n", rank);
    
    // // Unlock and destroy MPI windows
    mpi_window_t all_windows[] = {queue->q->win_items, queue->q->win_heads, 
                                 queue->q->win_row, queue->q->win_bitmap, 
                                 queue->q->win_sync_bitmap};
    mpi_win_unlock_all_multiple(all_windows, 5);
    
    mpi_win_destroy(&queue->q->win_items);
    mpi_win_destroy(&queue->q->win_heads);
    mpi_win_destroy(&queue->q->win_row);    
    mpi_win_destroy(&queue->q->win_bitmap);
    mpi_win_destroy(&queue->q->win_sync_bitmap);

    // Free memory if this is the producer
    if (spmc_queue_is_enqueuer(queue)) {
        // printf("[Rank %d] Cleaning up producer resources\n", rank);
        
        if (queue->q->items) {
            free(queue->q->items);
            queue->q->items = NULL;
        }
        if (queue->q->heads) {
            free(queue->q->heads);
            queue->q->heads = NULL;
        }
   
        // Producer has full bitmaps with data
        if (queue->q->bitmap) {
            if (queue->q->bitmap->data) {
                free(queue->q->bitmap->data);
                queue->q->bitmap->data = NULL;
            }
            free(queue->q->bitmap);
            queue->q->bitmap = NULL;
        }
        if (queue->q->sync_bitmap) {
            if (queue->q->sync_bitmap->data) {
                free(queue->q->sync_bitmap->data);
                queue->q->sync_bitmap->data = NULL;
            }
            free(queue->q->sync_bitmap);
            queue->q->sync_bitmap = NULL;
        }
        if (queue->p && queue->p->map) {
            if (queue->p->map->data) {
                free(queue->p->map->data);
                queue->p->map->data = NULL;
            }
            free(queue->p->map);
            queue->p->map = NULL;
        }
        
    } else {
        // printf("[Rank %d] Cleaning up consumer resources\n", rank);
        
        // Consumer cleanup - bitmap structures but no data
        // For consumers, bitmap data pointers are NULL, so only free structures
        if (queue->q->bitmap) {
            free(queue->q->bitmap);
            queue->q->bitmap = NULL;
        }
        if (queue->q->sync_bitmap) {
            free(queue->q->sync_bitmap);
            queue->q->sync_bitmap = NULL;
        }
        // Consumer map has data, so free it properly
        if (queue->c && queue->c->map) {
            if (queue->c->map->data) {
                free(queue->c->map->data);
                queue->c->map->data = NULL;
            }
            free(queue->c->map);
            queue->c->map = NULL;
        }
    }


    // Free structure memory
    if (queue->p) {
        // printf("[Rank %d] Freeing producer structure\n", rank);
        free(queue->p);
        queue->p = NULL;
    }
    if (queue->c) {
        // printf("[Rank %d] Freeing consumer structure\n", rank);
        free(queue->c);
        queue->c = NULL;
    }
    if (queue->q) {
        // printf("[Rank %d] Freeing queue structure\n", rank);
        free(queue->q);
        queue->q = NULL;
    }

    // printf("GWMQ SPMC Queue destroyed on rank %d\n", rank);
    mpi_finalize();
}

int spmc_queue_enqueue(spmc_queue_t *queue, int value) {
    if (!spmc_queue_is_enqueuer(queue)) return -1;
    
    int rank = mpi_get_rank(&queue->mpi_ctx);
    
    // O(k/64) worst case is O(N/64), almost O(1)
    // Find safe index from tail in map
    int old_tail = queue->p->tail;
    queue->p->tail = find_safe_index_from(queue->p->tail, queue->p->map, 0); // Using row 0 for producer map
    // printf("[Rank %d][ENQUEUE] find_safe_index_from(%d) returned %d\n", rank, old_tail, queue->p->tail);
    fflush(stdout);
    if (queue->p->tail == -1) {
        // fprintf(stderr, "[Rank %d][ENQUEUE ERROR] No safe index found in sync_bitmap\n", rank);
        return -1;
    }

    
    // Create new cell with value and generation
    cell_t new_cell = MAKE_CELL(value, queue->p->enq_row);

    
    // SWAP(ITEMS[tail], cell) - Using MPI_fetch_and_op with UINT64_T
    cell_t old_cell;
    MPI_Aint offset = queue->p->tail * sizeof(cell_t);
    
    int target_rank = queue->queue_owner_rank;  // Target the queue owner
    MPI_TRY(mpi_fetch_and_op(&new_cell, &old_cell, MPI_UINT64_T, target_rank, offset, 
                             MPI_REPLACE, &queue->q->win_items));
    

    // Check if we need to move to next row
    if (GET_DATA(old_cell) == T && GET_GEN(old_cell) == queue->p->enq_row) {
        
        // Synchronize bitmap from remote BITMAP[enq_row] to local sync_bitmap
        // MPI_GET: sync_bitmap = SYNC(BITMAP[enq_row])

       // sync_bitmap_row(queue, queue->p->enq_row, queue->p->map);
        print_bitmap(queue->p->map, 1, 64, "Producer local sync_bitmap after GET");
        // Heuristic: Based on the index we found need to reset and number of consumers
        int num_consumers = mpi_get_size(&queue->mpi_ctx) - 1;
        heuristic_bitmap(queue->p->map, queue->p->tail, num_consumers);

        // MPI_PUT: WRITE(SYNC_BITMAP[enq_row], sync_bitmap)
        int words = queue->q->sync_bitmap->words_per_row;
        MPI_Aint sync_offset = queue->p->enq_row * words * sizeof(uint64_t);

        MPI_TRY(mpi_put(queue->p->map->data, words * sizeof(uint64_t), MPI_BYTE,
                        target_rank, sync_offset,
                        &queue->q->win_sync_bitmap));
        
        // Move to next row
        queue->p->enq_row += 1;
        queue->p->tail = 0;
        // printf("[Rank %d][ENQUEUE] Moving to next row: enq_row=%d, tail reset to 0\n", rank, enq_row);
        
        // Find safe index again and retry swap
        queue->p->tail = find_safe_index_from(queue->p->tail, queue->p->map, 0); // Still use row 0 for producer map
        if (queue->p->tail == -1) {
            // fprintf(stderr, "No safe index found after row increment\n");
            return -1;
        }
        
        // Create new cell with updated generation
        new_cell = MAKE_CELL(value, queue->p->enq_row);
        
        // SWAP again with new row
        cell_t old_cell_retry;
        MPI_Aint offset = queue->p->tail * sizeof(cell_t);

        MPI_TRY(mpi_fetch_and_op(&new_cell, &old_cell_retry, MPI_UINT64_T, target_rank, offset, 
                             MPI_REPLACE, &queue->q->win_items));
        
        // printf("[Rank %d][ENQUEUE] (2nd try) Swapped at tail %d (row %d), old_cell: {data=%d, gen=%d}, new_cell: {data=%d, gen=%d}\n", 
        //    rank, queue->p->tail, queue->p->enq_row, GET_DATA(old_cell_retry), GET_GEN(old_cell_retry), GET_DATA(new_cell), GET_GEN(new_cell));
        
        // MPI_Accumulate + MPI_REPLACE (atomic write): WRITE(ROW, enq_row)
        MPI_TRY(mpi_accumulate(&queue->p->enq_row, 1, MPI_INT, target_rank, 0,
                              MPI_REPLACE, &queue->q->win_row));
    }
    
    // Update local producer state (chạy sau cả normal enqueue VÀ row transition)
    // Don't increment tail linearly - let find_safe_index_from handle it dynamically
    // Only update tail for starting hint in next enqueue, but will be overridden by find_safe_index_from
    queue->p->tail = (queue->p->tail + 1) % MAX_QUEUE_SIZE;  // Just a hint for next search
    
    return MPI_SUCCESS;
}

int spmc_queue_dequeue(spmc_queue_t *queue, int *out_data, int max_count) {
    if (spmc_queue_is_enqueuer(queue) || !out_data || max_count <= 0) return 0;
    
    // BBQ only supports single dequeue
    int rank = mpi_get_rank(&queue->mpi_ctx);
    int consumer_idx = rank - 1;  // Consumer index (0-based)
    int target_rank = queue->queue_owner_rank;  // Target the queue owner
    
    // double dequeue_start = get_time_us();
    
    // [TIMER 1] READ(ROW) - atomic read operation
    // double read_row_start = get_time_us();
    // printf("[Rank %d][DEQUEUE] Starting dequeue operation...\n", rank);
    fflush(stdout);
    
    int deq_row;
    int zero = 0;

    MPI_TRY(mpi_fetch_and_op(&zero, &deq_row, MPI_INT, target_rank, 0, 
                             MPI_NO_OP, &queue->q->win_row));

    // print_timing("READ_ROW", read_row_start, rank);

    
    int is_new_row = (deq_row != queue->c->last_deq_row);

    
    if (is_new_row && deq_row > 0) {
        // [TIMER 2] SYNC_BITMAP read - potential bottleneck for multiple consumers
        // double sync_bitmap_start = get_time_us();
        
        // MPI_GET: sync_bitmap = READ(SYNC_BITMAP[deq_row - 1])
        int words = queue->q->sync_bitmap->words_per_row;
        MPI_Aint offset = (deq_row - 1) * words * sizeof(uint64_t);
        
        MPI_TRY(mpi_get(queue->c->map->data, words * sizeof(uint64_t), MPI_BYTE,
                        target_rank, offset,
                        &queue->q->win_sync_bitmap));
        
        // print_timing("SYNC_BITMAP_READ", sync_bitmap_start, rank);
        
        queue->c->last_deq_row = deq_row;
        queue->c->last_value = T;
        queue->c->last_index = -1;
        queue->c->last_N = -1;
        // printf("[Rank %d][DEQUEUE] Updated last_deq_row to %d\n", rank, deq_row);
    } else if (!is_new_row && queue->c->last_value == L) {
        // printf("[Rank %d][DEQUEUE] No new row and last value was L - returning empty\n", rank);
        // usleep(100000);  // REMOVED: This was causing severe performance degradation
        return 0;  // No items dequeued
    }
    
    // printf("[Rank %d][DEQUEUE] Passed early-exit checks, last_value=%d\n", rank, queue->c->last_value);
    fflush(stdout);
    
    // [TIMER 3] HEADS contention - major serialization bottleneck
    // double heads_start = get_time_us();
    int head;
    int one = 1;
    MPI_Aint head_offset = deq_row;  // No need to multiply by sizeof(int), MPI window displacement unit handles it
    
    // printf("[Rank %d][DEQUEUE] About to fetch_and_add HEADS at offset %ld...\n", rank, (long)head_offset);
    fflush(stdout);
    MPI_TRY(mpi_fetch_and_op(&one, &head, MPI_INT, target_rank, head_offset,
                             MPI_SUM, &queue->q->win_heads));
    // printf("[Rank %d][DEQUEUE] Fetched HEAD = %d\n", rank, head);
    fflush(stdout);
    // print_timing("HEADS_FETCH_AND_ADD", heads_start, rank);
    
    // [TIMER 4] Bitmap search - O(d/64) complexity
    // double bitmap_search_start = get_time_us();
    
    // Reset optimization if we're in a new row
    if (is_new_row) {
        queue->c->last_index = -1;
        queue->c->last_N = -1;
    }
    
    int index = find_Nth_safe_index(head, queue->c->map, 0, queue->c->last_index, queue->c->last_N);
    // print_timing("BITMAP_SEARCH", bitmap_search_start, rank);
    if (index == -1) {
        // fprintf(stderr, "[Rank %d][DEQUEUE ERROR] No safe index found for head=%d\n", rank, head);
        queue->c->last_value = L;  // Set last_value to L when no safe index found
        return 0;  // No items dequeued
    }
    
    // Update optimization state for next call
    queue->c->last_index = index;
    queue->c->last_N = head;
    
    // printf("[Rank %d][DEQUEUE] Found %dth safe index at position %d\n", rank, head, index);
    
    // [TIMER 5] Atomic SWAP operation on items array
    // double swap_start = get_time_us();
    
    // Create cell with T marker and current row
    cell_t new_cell = MAKE_CELL(T, deq_row);
    
    // SWAP(ITEMS[index], cell) - Using MPI_fetch_and_op with UINT64_T
    cell_t old_cell;
    MPI_Aint offset = index * sizeof(cell_t);
    
    MPI_TRY(mpi_fetch_and_op(&new_cell, &old_cell, MPI_UINT64_T, target_rank, offset, 
                             MPI_REPLACE, &queue->q->win_items));
    // print_timing("ATOMIC_SWAP", swap_start, rank);
    
    // [TIMER 6] Bitmap marking operation
    // double bitmap_mark_start = get_time_us();
    
    // Mark this index as dequeued in BITMAP
    // MPI_fetch_op + MPI_BOR: WRITE(BITMAP[deq_row][index], 1)
    // Need to implement atomic bit set operation
    // Using MPI_Accumulate with MPI_BOR on the appropriate word
    int word_index = index / 64;
    int bit_offset = index % 64;
    uint64_t bit_mask = (1ULL << bit_offset);
    MPI_Aint bitmap_offset = (deq_row * queue->q->bitmap->words_per_row + word_index) * sizeof(uint64_t);
    
    MPI_TRY(mpi_accumulate(&bit_mask, 1, MPI_UINT64_T, target_rank, bitmap_offset,
                          MPI_BOR, &queue->q->win_bitmap));
    // print_timing("BITMAP_MARK", bitmap_mark_start, rank);
    
    // Check return value
    if (GET_DATA(old_cell) == L || GET_GEN(old_cell) != deq_row) {
        printf("[Rank %d][DEQUEUE] Invalid cell: data=%d (L=-1), gen=%d, head=%d, index=%d\n", 
               rank, GET_DATA(old_cell), GET_GEN(old_cell), head, index);
        queue->c->last_value = L;
        // print_timing("DEQUEUE_TOTAL", dequeue_start, rank);
        // usleep(100);  // REMOVED: This was causing ~100μs penalty per failed dequeue
        return 0;  // No items dequeued
    } else {
        // printf("[Rank %d][DEQUEUE] Successfully dequeued value: %d at index=%d, head=%d\n", rank, GET_DATA(old_cell), index, head);
        // print_timing("DEQUEUE_TOTAL", dequeue_start, rank);
        out_data[0] = GET_DATA(old_cell);  // Store the dequeued value
        return 1;  // Successfully dequeued 1 item
    }
}

void spmc_queue_print_stats(spmc_queue_t *queue) {
    if (!queue) return;
    
    int rank = mpi_get_rank(&queue->mpi_ctx);
    int size = mpi_get_size(&queue->mpi_ctx);
    
    printf("GWMQ SPMC Queue Stats - Rank %d/%d:\n", rank, size);
    
    if (spmc_queue_is_enqueuer(queue)) {
        printf("  Producer tail: %d\n", queue->p->tail);
        printf("  Producer enq_row: %d\n", queue->p->enq_row);
        printf("  Structure row: %d\n", queue->q->row);
        printf("  Queue size: %d\n", MAX_QUEUE_SIZE);
    } else {
        printf("  Consumer last_deq_row: %d\n", queue->c->last_deq_row);
        printf("  Queue size: %d\n", queue->c->size);
    }
    
    // TODO: Add bitmap statistics when implemented
}

int spmc_queue_is_enqueuer(spmc_queue_t *queue) {
    if (!queue) return 0;
    // Producer is always rank 0
    return (mpi_get_rank(&queue->mpi_ctx) == 0);
}

int spmc_queue_get_batch_size(spmc_queue_t *queue) {
    // BBQ doesn't support efficient batch dequeue, use single dequeue
    return 1;
}

size_t spmc_queue_get_capacity_bytes(const spmc_queue_t *queue) {
    if (!queue) return 0;
    
    size_t total_size = 0;
    
    // Base structure sizes
    total_size += sizeof(producer_t);
    total_size += sizeof(consumer_t); 
    total_size += sizeof(structure_t);
    
    if (spmc_queue_is_enqueuer((spmc_queue_t*)queue)) {
        // Items array
        total_size += MAX_QUEUE_SIZE * sizeof(cell_t);
        
        // Heads array (one per consumer)
        int num_consumers = mpi_get_size(&queue->mpi_ctx) - 1;
        total_size += num_consumers * sizeof(int);
        
        // TODO: Add bitmap sizes when implemented
        // total_size += bitmap_memory_size(queue->q->rows, queue->q->cols) * 3; // 3 bitmaps
    }
    
    return total_size;
}
