#include "spmc_queue.h"
#include "bitmap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


int spmc_queue_init(spmc_queue_t *queue, int argc, char *argv[]){
    if (!queue) return -1;

    // Initialize MPI context
    MPI_TRY(mpi_init(argc, argv, &queue->mpi_ctx));

    if (mpi_get_size(&queue->mpi_ctx) < 2) {
        fprintf(stderr, "At least two processes (1 producer, 1+ consumer) are required.\n");
        mpi_finalize();
        return -1;
    }

    int rank = mpi_get_rank(&queue->mpi_ctx);
    int size = mpi_get_size(&queue->mpi_ctx);
    int is_producer = (rank == 0);

    // Initialize pointers to NULL
    queue->p = NULL;
    queue->c = NULL;

    // Initialize Structure (main queue structure)
    queue->q = malloc(sizeof(structure_t));
    if (!queue->q) {
        fprintf(stderr, "Failed to allocate memory for Structure.\n");
        mpi_finalize();
        return -1;
    }

    // For now, using placeholder values - need to implement proper sizing logic
    int rows = MAX_ROW;  
    int cols = MAX_QUEUE_SIZE;

    if (is_producer) {
        
           // Initialize Producer structure
        queue->p = malloc(sizeof(producer_t));
        if (!queue->p) {
            fprintf(stderr, "Failed to allocate memory for Producer structure.\n");
            free(queue->q);
            mpi_finalize();
            return -1;
        }

        // Initialize items array
        queue->q->items = malloc(MAX_QUEUE_SIZE * sizeof(cell_t));
        if (!queue->q->items) {
            fprintf(stderr, "Failed to allocate memory for queue items.\n");
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
            fprintf(stderr, "Failed to allocate memory for heads array.\n");
            // TODO: Add proper cleanup
            return -1;
        }
        
        // Initialize all heads to 0
        for (int i = 0; i < num_consumers; i++) {
            queue->q->heads[i] = 0;
        }

        // Initialize producer-specific fields
        queue->p->tail = 0;
        queue->p->enq_row = 0;
        queue->p->size = MAX_QUEUE_SIZE;
        
        queue->p->map = malloc(sizeof(bitmap_t));
        queue->q->bitmap = malloc(sizeof(bitmap_t));
        queue->q->sync_bitmap = malloc(sizeof(bitmap_t));

        bitmap_init(queue->p->map, 1, cols);
        bitmap_init(queue->q->bitmap, rows, cols);
        bitmap_init(queue->q->sync_bitmap, rows, cols);
        
        // Initialize sync_bitmap with all 1s as per algorithm: [1,1,1,1,...]
        // set_all_bits(queue->q->bitmap, 0);
        
        // Initialize producer map with all 1s
        set_all_bits_full(queue->p->map);

        // Initialize Structure fields
        queue->q->row = 0;
        // queue->q->rows = rows;
        // queue->q->cols = cols;

    } else {
        // Initialize Consumer structure  
        queue->c = malloc(sizeof(consumer_t));
        if (!queue->c) {
            fprintf(stderr, "Failed to allocate memory for Consumer structure.\n");
            free(queue->q);
            mpi_finalize();
            return -1;
        }
        // Consumers don't allocate main memory
        queue->q->items = NULL;
        queue->q->heads = NULL;
        queue->q->row = 0;
        // queue->q->rows = rows;
        // queue->q->cols = cols;
        queue->c->size = MAX_QUEUE_SIZE;
        queue->c->last_value = T;
        queue->c->last_deq_row = 0;
        // Initialize optimization variables
        queue->c->last_index = -1;
        queue->c->last_N = -1;
        
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
        
        queue->c->map = malloc(sizeof(bitmap_t));
        bitmap_init(queue->c->map, 1, cols);
        set_all_bits_full(queue->c->map);
    }

    // Create MPI windows for one-sided access
    size_t items_size = is_producer ? MAX_QUEUE_SIZE * sizeof(cell_t) : 0;
    size_t heads_size = is_producer ? (size - 1) * sizeof(int) : 0;
    size_t row_size = is_producer ? sizeof(int) : 0;
    size_t bitmap_size = is_producer ? (MPI_Aint)queue->q->bitmap->rows * queue->q->bitmap->words_per_row * sizeof(uint64_t) : 0;


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
    printf("GWMQ SPMC Queue initialized on rank %d/%d\n", rank, size);

    return MPI_SUCCESS;
}


void spmc_queue_destroy(spmc_queue_t *queue) {
    if (!queue) return;
    print_queue_bitmaps(queue, 5, 5);
    int rank = mpi_get_rank(&queue->mpi_ctx);
    printf("[Rank %d] Starting queue destruction\n", rank);
    
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
        printf("[Rank %d] Cleaning up producer resources\n", rank);
        
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
        printf("[Rank %d] Cleaning up consumer resources\n", rank);
        
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
        printf("[Rank %d] Freeing producer structure\n", rank);
        free(queue->p);
        queue->p = NULL;
    }
    if (queue->c) {
        printf("[Rank %d] Freeing consumer structure\n", rank);
        free(queue->c);
        queue->c = NULL;
    }
    if (queue->q) {
        printf("[Rank %d] Freeing queue structure\n", rank);
        free(queue->q);
        queue->q = NULL;
    }

    printf("GWMQ SPMC Queue destroyed on rank %d\n", rank);
    mpi_finalize();
}

int spmc_queue_enqueue(spmc_queue_t *queue, int value) {
    if (!spmc_queue_is_enqueuer(queue)) return -1;
    
    int rank = mpi_get_rank(&queue->mpi_ctx);
    
    // Local variables for producer
    int tail = queue->p->tail;
    int enq_row = queue->p->enq_row;
    
    
    // O(k/64) worst case is O(N/64), almost O(1)
    // Find safe index from tail in map - FIX: use enq_row instead of 0
    tail = find_safe_index_from(tail, queue->p->map, 0); // Using row 0 for producer map
    if (tail == -1) {
        fprintf(stderr, "[Rank %d][ENQUEUE ERROR] No safe index found in sync_bitmap\n", rank);
        return -1;
    }

    
    // Create new cell with value and generation
    cell_t new_cell = MAKE_CELL(value, enq_row);

    
    // SWAP(ITEMS[tail], cell) - Using MPI_fetch_and_op with UINT64_T
    cell_t old_cell;
    MPI_Aint offset = tail * sizeof(cell_t);
    
    MPI_TRY(mpi_fetch_and_op(&new_cell, &old_cell, MPI_UINT64_T, 0, offset, 
                             MPI_REPLACE, &queue->q->win_items));
    
    printf("[Rank %d][ENQUEUE] Swapped at tail %d (row %d), old_cell: {data=%d, gen=%d}, new_cell: {data=%d, gen=%d}\n", 
           rank, tail, enq_row, GET_DATA(old_cell), GET_GEN(old_cell), GET_DATA(new_cell), GET_GEN(new_cell));
    
    // Check if we need to move to next row
    if (GET_DATA(old_cell) == T && GET_GEN(old_cell) == enq_row) {
        
        // Synchronize bitmap from remote BITMAP[enq_row] to local sync_bitmap
        // MPI_GET: sync_bitmap = SYNC(BITMAP[enq_row])

        sync_bitmap_row(queue, enq_row, queue->p->map);

        // Heuristic: Based on the index we found need to reset and number of consumers
        int num_consumers = mpi_get_size(&queue->mpi_ctx) - 1;
        heuristic_bitmap(queue->p->map, tail, num_consumers);
        
        // MPI_PUT: WRITE(SYNC_BITMAP[enq_row], sync_bitmap)
        int words = queue->q->sync_bitmap->words_per_row;
        MPI_Aint sync_offset = enq_row * words * sizeof(uint64_t);

        MPI_TRY(mpi_put(queue->p->map->data, words * sizeof(uint64_t), MPI_BYTE,
                        0, sync_offset,
                        &queue->q->win_sync_bitmap));
        
        // Move to next row
        enq_row += 1;
        tail = 0;
        // printf("[Rank %d][ENQUEUE] Moving to next row: enq_row=%d, tail reset to 0\n", rank, enq_row);
        
        // Find safe index again and retry swap
        tail = find_safe_index_from(tail, queue->p->map, 0); // Still use row 0 for producer map
        if (tail == -1) {
            fprintf(stderr, "No safe index found after row increment\n");
            return -1;
        }
        
        // Create new cell with updated generation
        new_cell = MAKE_CELL(value, enq_row);
        
        // SWAP again with new row
        cell_t old_cell_retry;
        MPI_Aint offset = tail * sizeof(cell_t);

        MPI_TRY(mpi_fetch_and_op(&new_cell, &old_cell_retry, MPI_UINT64_T, 0, offset, 
                             MPI_REPLACE, &queue->q->win_items));
        
        printf("[Rank %d][ENQUEUE] (2nd try) Swapped at tail %d (row %d), old_cell: {data=%d, gen=%d}, new_cell: {data=%d, gen=%d}\n", 
           rank, tail, enq_row, GET_DATA(old_cell_retry), GET_GEN(old_cell_retry), GET_DATA(new_cell), GET_GEN(new_cell));
        
        // MPI_Accumulate + MPI_REPLACE (atomic write): WRITE(ROW, enq_row)
        MPI_TRY(mpi_accumulate(&enq_row, 1, MPI_INT, 0, 0,
                              MPI_REPLACE, &queue->q->win_row));
    }
    
    // FIX: Update local producer state - don't increment tail linearly
    // Instead, find next safe index for next enqueue
    int next_tail = (tail + 1) % MAX_QUEUE_SIZE;
    queue->p->tail = next_tail;
    queue->p->enq_row = enq_row;
    
    
    return MPI_SUCCESS;
}

int spmc_queue_dequeue(spmc_queue_t *queue) {
    if (spmc_queue_is_enqueuer(queue)) return -1;  // Only consumers can dequeue
    
    int rank = mpi_get_rank(&queue->mpi_ctx);
    int consumer_idx = rank - 1;  // Consumer index (0-based)
    // printf("[Rank %d][DEQUEUE] Starting dequeue operation (Consumer %d)\n", rank, consumer_idx);
    
    // MPI_fetch_op + MPI_NO_OP (atomic read): READ(ROW)
    int deq_row;
    int zero = 0;
    MPI_TRY(mpi_fetch_and_op(&zero, &deq_row, MPI_INT, 0, 0, 
                             MPI_NO_OP, &queue->q->win_row));

    
    int is_new_row = (deq_row != queue->c->last_deq_row);
    
    if (is_new_row && deq_row > 0) {
        // printf("[Rank %d][DEQUEUE] Detected new row. Synchronizing with SYNC_BITMAP row %d\n", 
        //        rank, deq_row - 1);
               
        // MPI_GET: sync_bitmap = READ(SYNC_BITMAP[deq_row - 1])
        int words = queue->q->sync_bitmap->words_per_row;
        MPI_Aint offset = (deq_row - 1) * words * sizeof(uint64_t);
        
        // printf("[Rank %d][DEQUEUE] Reading %d words from SYNC_BITMAP offset %ld\n", 
        //        rank, words, offset);
        MPI_TRY(mpi_get(queue->c->map->data, words * sizeof(uint64_t), MPI_BYTE,
                        0, offset,
                        &queue->q->win_sync_bitmap));
        
        queue->c->last_deq_row = deq_row;
        queue->c->last_value = T;
        queue->c->last_index = -1;
        queue->c->last_N = -1;
        printf("[Rank %d][DEQUEUE] Updated last_deq_row to %d\n", rank, deq_row);
    } else if (!is_new_row && queue->c->last_value == L) {
        printf("[Rank %d][DEQUEUE] No new row and last value was L - returning empty\n", rank);
        usleep(100);
        return -1;
    }
    
    // Fetch and Add on HEADS[consumer_rank] to get unique head value
    // MPI_fetch_op + MPI_SUM (Fetch and Add)
    int head;
    int one = 1;
    MPI_Aint head_offset = deq_row * sizeof(int);

    
    MPI_TRY(mpi_fetch_and_op(&one, &head, MPI_INT, 0, head_offset,
                             MPI_SUM, &queue->q->win_heads));
    // printf("[Rank %d][DEQUEUE] Received head value: %d\n", rank, head);
    
    // O(d/64), almost O(1) - optimized with previous results
    // Find the Nth safe index where N = head
    // printf("[Rank %d][DEQUEUE] Finding %dth safe index in consumer map\n", rank, head);
    
    // Reset optimization if we're in a new row
    if (is_new_row) {
        queue->c->last_index = -1;
        queue->c->last_N = -1;
    }
    
    int index = find_Nth_safe_index(head, queue->c->map, 0, queue->c->last_index, queue->c->last_N);
    if (index == -1) {
        fprintf(stderr, "[Rank %d][DEQUEUE ERROR] No safe index found for head=%d\n", rank, head);
        queue->c->last_value = L;  // Set last_value to L when no safe index found
        return -1;  // None - no item available
    }
    
    // Update optimization state for next call
    queue->c->last_index = index;
    queue->c->last_N = head;
    
    // printf("[Rank %d][DEQUEUE] Found %dth safe index at position %d\n", rank, head, index);
    
    // Create cell with T marker and current row
    cell_t new_cell = MAKE_CELL(T, deq_row);
    
    // SWAP(ITEMS[index], cell) - Using MPI_fetch_and_op with UINT64_T
    cell_t old_cell;
    MPI_Aint offset = index * sizeof(cell_t);
    
    // printf("[Rank %d][DEQUEUE] Reading cell at index %d, offset %ld\n", rank, index, offset);
    
    MPI_TRY(mpi_fetch_and_op(&new_cell, &old_cell, MPI_UINT64_T, 0, offset, 
                             MPI_REPLACE, &queue->q->win_items));
    
    // Mark this index as dequeued in BITMAP
    // MPI_fetch_op + MPI_BOR: WRITE(BITMAP[deq_row][index], 1)
    // Need to implement atomic bit set operation
    // Using MPI_Accumulate with MPI_BOR on the appropriate word
    int word_index = index / 64;
    int bit_offset = index % 64;
    uint64_t bit_mask = (1ULL << bit_offset);
    MPI_Aint bitmap_offset = (deq_row * queue->q->bitmap->words_per_row + word_index) * sizeof(uint64_t);
    
    // printf("[Rank %d][DEQUEUE] Marking bit in BITMAP[%d][%d]: word=%d, bit=%d, offset=%ld\n", 
    //        rank, deq_row, index, word_index, bit_offset, bitmap_offset);
    MPI_TRY(mpi_accumulate(&bit_mask, 1, MPI_UINT64_T, 0, bitmap_offset,
                          MPI_BOR, &queue->q->win_bitmap));
    
    // Check return value
    if (GET_DATA(old_cell) == L || GET_GEN(old_cell) != deq_row) {
        printf("[Rank %d][DEQUEUE] Invalid cell: data=%d (L=%d), gen=%d, head=%d, index=%d\n", 
               rank, GET_DATA(old_cell), L, GET_GEN(old_cell), head, index);
        queue->c->last_value = L;
        usleep(100);  // Small sleep to avoid busy looping
        return -1;  // None - empty or wrong generation
    } else {
        printf("[Rank %d][DEQUEUE] Successfully dequeued value: %d at index=%d, head=%d\n", rank, GET_DATA(old_cell), index, head);
        return GET_DATA(old_cell);  // Return the dequeued value
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
    return (mpi_get_rank(&queue->mpi_ctx) == 0);
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
