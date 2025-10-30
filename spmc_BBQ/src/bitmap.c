#include "bitmap.h"
#include "spmc_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * @brief Initializes a Bitmap structure.
 */
void bitmap_init(bitmap_t* map, int rows, int cols) {
    map->rows = rows;
    map->cols = cols;
    map->words_per_row = (cols + 63) / 64;
    map->data = calloc(rows * map->words_per_row, sizeof(uint64_t));
}

/**
 * @brief Destroys a Bitmap structure.
 */
void bitmap_destroy(bitmap_t* map) {
    if (map) {
        free(map->data);
        free(map);
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

/**
 * @brief Set a bit in the bitmap
 */
void set_bit(uint64_t *row, int col) {
    int word = col / 64;
    int bit  = col % 64;
    row[word] |= (1ULL << bit);
}

/**
 * @brief Clear a bit in the bitmap (set to 0)
 */
void clear_bit(uint64_t *row, int col) {
    int word = col / 64;
    int bit  = col % 64;
    row[word] &= ~(1ULL << bit);
}

/**
 * @brief Set all bits in a bitmap row to 1 (full 1s)
 * This initializes sync_bitmap as [1,1,1,1,...]
 */
void set_all_bits(bitmap_t* bitmap, int row) {
    if (!bitmap || row >= bitmap->rows) return;
    
    uint64_t* bitmap_row = &bitmap->data[row * bitmap->words_per_row];
    int words = bitmap->words_per_row;
    int cols = bitmap->cols;
    
    // Set all complete words to all 1s
    for (int i = 0; i < words - 1; i++) {
        bitmap_row[i] = 0xFFFFFFFFFFFFFFFFULL;  // All 64 bits set to 1
    }
    
    // Handle the last word - only set bits up to cols
    int remaining_bits = cols % 64;
    if (remaining_bits == 0) {
        // If cols is multiple of 64, set all bits in last word
        bitmap_row[words - 1] = 0xFFFFFFFFFFFFFFFFULL;
    } else {
        // Set only the needed bits in the last word
        uint64_t mask = (1ULL << remaining_bits) - 1;
        bitmap_row[words - 1] = mask;
    }
}

/**
 * @brief Set all bits in entire bitmap to 1 (all rows)
 */
void set_all_bits_full(bitmap_t* bitmap) {
    if (!bitmap) return;
    
    for (int row = 0; row < bitmap->rows; row++) {
        set_all_bits(bitmap, row);
    }
}

/**
 * @brief Find safe index from starting position using sync_bitmap
 * Returns first index >= start where bit is 1 (safe to use)
 * O(k/64) complexity, almost O(1)
 * NOTE: Does NOT wrap around - only searches from start to end
 */
int find_safe_index_from(int start, bitmap_t* sync_bitmap, int row) {
    int cols = sync_bitmap->cols;
    uint64_t* bitmap_row = &sync_bitmap->data[row * sync_bitmap->words_per_row];
    
    // In GWMQ: bit=1 means safe, bit=0 means unsafe (being used by consumer)
    // Tìm kiếm từ start đến cuối, KHÔNG wrap around
    for (int i = start; i < cols; i++) {
        if (check_bit(bitmap_row, i)) {  // Check for bit=1 (safe)
            return i;
        }
    }
    
    // If not found, wrap around from beginning
    for (int i = 0; i < start; i++) {
        if (check_bit(bitmap_row, i)) {  // Check for bit=1 (safe)
            return i;
        }
    }
    
    // Không tìm thấy index nào >= start có bit=1
    return -1;
}

/**
 * @brief Find the Nth safe index in sync_bitmap (0-indexed) with optimization
 * N=0 returns first set bit, N=1 returns second set bit, etc.
 * O(d/64) complexity, almost O(1) - optimized with last_index and last_N
 * @param N Target index to find (0-based)
 * @param sync_bitmap Bitmap to search in
 * @param row Row to search in bitmap
 * @param last_index Previous found index (-1 if first call)
 * @param last_N Previous N value (-1 if first call)
 * @return Index of Nth set bit, or -1 if not found
 */
int find_Nth_safe_index(int N, bitmap_t* sync_bitmap, int row, int last_index, int last_N) {
    if (N < 0) return -1;

    int words_per_row = sync_bitmap->words_per_row;
    uint64_t* bitmap_row = &sync_bitmap->data[row * words_per_row];
    
    // Optimization: If we can use previous result as starting point
    if (last_index >= 0 && last_N >= 0 && N > last_N) {
        int remaining_N = N - last_N - 1; // Số bit còn lại cần tìm từ sau last_index
        int start_word = (last_index + 1) / 64; // Word bắt đầu tìm kiếm
        int start_bit = (last_index + 1) % 64;  // Bit bắt đầu trong word đó
        
        // Tìm kiếm từ vị trí sau last_index
        for (int i = start_word; i < words_per_row; i++) {
            uint64_t current_word = bitmap_row[i];
            
            // Nếu đang ở word đầu tiên, mask out các bit trước start_bit
            if (i == start_word && start_bit > 0) {
                // Tạo mask để loại bỏ các bit từ 0 đến start_bit-1
                uint64_t mask = ~((1ULL << start_bit) - 1);
                current_word &= mask;
            }
            
            if (current_word == 0) {
                continue; // Bỏ qua word toàn số 0
            }

            // Đếm số bit 1 trong word hiện tại
            int popcount = __builtin_popcountll(current_word);

            if (remaining_N < popcount) {
                // Bit cần tìm nằm trong word này!
                int bit_pos_in_word = find_nth_set_bit_in_word(current_word, remaining_N + 1);
                return i * 64 + bit_pos_in_word;
            } else {
                // Bit cần tìm nằm ở các word sau, giảm remaining_N đi
                remaining_N -= popcount;
            }
        }
        
        // Nếu không tìm thấy trong phần tối ưu, fall back về tìm kiếm từ đầu
    }
    
    // Tìm kiếm thông thường từ đầu bitmap (fallback hoặc khi không có optimization)
    int search_N = N;
    for (int i = 0; i < words_per_row; i++) {
        uint64_t current_word = bitmap_row[i];
        if (current_word == 0) {
            continue; // Bỏ qua word toàn số 0
        }

        // Đếm số bit 1 trong word hiện tại
        int popcount = __builtin_popcountll(current_word);

        if (search_N < popcount) {
            // Bit cần tìm nằm trong word này! (0-indexed: N < popcount)
            // Tìm chỉ số của bit thứ N trong word này (N+1 vì helper function là 1-indexed)
            int bit_pos_in_word = find_nth_set_bit_in_word(current_word, search_N + 1);
            // Tính chỉ số cuối cùng
            return i * 64 + bit_pos_in_word;
        } else {
            // Bit cần tìm nằm ở các word sau, giảm N đi
            search_N -= popcount;
        }
    }

    return -1; // Không tìm thấy bit thứ N
}

// Hàm helper để tìm vị trí của bit thứ N trong một word 64-bit (1-indexed)
// n=1 returns first set bit, n=2 returns second set bit, etc.
// Trả về chỉ số bit (0-63)
int find_nth_set_bit_in_word(uint64_t word, int n) {
    // Dùng __builtin_ctzll (Count Trailing Zeros) để tìm bit 1 thấp nhất
    // và xóa nó đi cho đến khi tìm được bit thứ n.
    // Lưu ý: __builtin_... là của GCC/Clang. Cần có giải pháp khác cho MSVC.
    for (int i = 1; i < n; ++i) {
        // Xóa bit 1 thấp nhất
        word &= word - 1;
    }
    return __builtin_ctzll(word);
}

/**
 * @brief Print bitmap content with options for rows and columns
 * @param bitmap Pointer to bitmap to print
 * @param max_rows Maximum number of rows to print (-1 for all)
 * @param max_cols Maximum number of columns to print (-1 for all)
 * @param title Title to display before bitmap
 */
void print_bitmap(bitmap_t* bitmap, int max_rows, int max_cols, const char* title) {
    if (!bitmap || !bitmap->data) {
        printf("%s: NULL or empty bitmap\n", title ? title : "Bitmap");
        return;
    }
    
    int rows_to_print = (max_rows == -1) ? bitmap->rows : 
                        (max_rows < bitmap->rows ? max_rows : bitmap->rows);
    int cols_to_print = (max_cols == -1) ? bitmap->cols : 
                        (max_cols < bitmap->cols ? max_cols : bitmap->cols);
    
    printf("\n=== %s ===\n", title ? title : "Bitmap");
    printf("Size: %dx%d (showing %dx%d)\n", 
           bitmap->rows, bitmap->cols, rows_to_print, cols_to_print);
    printf("Words per row: %d\n", bitmap->words_per_row);
    
    // Print column headers
    printf("Row\\Col ");
    for (int col = 0; col < cols_to_print; col++) {
        if (col % 10 == 0) {
            printf("%d", (col / 10) % 10);
        } else {
            printf(" ");
        }
    }
    printf("\n      ");
    for (int col = 0; col < cols_to_print; col++) {
        printf("%d", col % 10);
    }
    printf("\n");
    
    // Print bitmap rows
    for (int row = 0; row < rows_to_print; row++) {
        printf("%3d:  ", row);
        uint64_t* bitmap_row = &bitmap->data[row * bitmap->words_per_row];
        
        for (int col = 0; col < cols_to_print; col++) {
            int bit_value = check_bit(bitmap_row, col);
            printf("%c", bit_value ? '1' : '.');
        }
        
        // Print row statistics
        int set_bits = 0;
        for (int col = 0; col < bitmap->cols; col++) {
            if (check_bit(bitmap_row, col)) {
                set_bits++;
            }
        }
        printf("  (%d/%d bits set)", set_bits, bitmap->cols);
        printf("\n");
    }
    
    if (rows_to_print < bitmap->rows) {
        printf("... (%d more rows)\n", bitmap->rows - rows_to_print);
    }
    printf("\n");
}

/**
 * @brief Print all bitmaps in the queue for debugging
 * @param queue Pointer to SPMC queue
 * @param max_rows Maximum rows to show for each bitmap
 * @param max_cols Maximum columns to show for each bitmap
 */
void print_queue_bitmaps(struct spmc_queue *queue, int max_rows, int max_cols) {
    if (!queue) {
        printf("Queue is NULL\n");
        return;
    }
    
    int rank = mpi_get_rank(&queue->mpi_ctx);
    printf("\n========== QUEUE BITMAPS DEBUG (Rank %d) ==========\n", rank);
    
    if (spmc_queue_is_enqueuer(queue)) {
        // Producer bitmaps
        print_bitmap(queue->p->map, max_rows, max_cols, "Producer Map");
        print_bitmap(queue->q->bitmap, max_rows, max_cols, "Main Bitmap");
        print_bitmap(queue->q->sync_bitmap, max_rows, max_cols, "Sync Bitmap");
    } else {
        // Consumer bitmaps
        print_bitmap(queue->c->map, max_rows, max_cols, "Consumer Map");
        printf("Main Bitmap: Not accessible from consumer\n");
        printf("Sync Bitmap: Not accessible from consumer\n");
    }
    
    printf("===============================================\n\n");
}

/**
 * @brief Synchronize bitmap from remote process
 * Copies the bitmap row from BITMAP to local sync_bitmap
 */
void sync_bitmap_row(struct spmc_queue *queue, int row, bitmap_t* local_bitmap) {
    int words = queue->q->bitmap->words_per_row;
    MPI_Aint offset = row * words * sizeof(uint64_t);
    uint64_t* local_row = &local_bitmap->data[0];  // Store in first row of local bitmap
    
    MPI_TRY(mpi_get_accumulate(local_row, words * sizeof(uint64_t), MPI_BYTE,
                                 local_row, words * sizeof(uint64_t), MPI_BYTE,
                                 0, offset, words * sizeof(uint64_t), MPI_BYTE,
                                 MPI_NO_OP, &queue->q->win_bitmap));

}

/**
 * @brief Heuristic to update sync_bitmap based on found index
 * After syncing from BITMAP (which may be all 0s), set up the local map:
 * - Clear bits that consumers are predicted to use (found_index+1 to found_index+num_consumers)
 * - Set all bits AFTER the cleared range to 1 (safe/available for producer)
 */
void heuristic_bitmap(bitmap_t* bitmap, int found_index, int num_consumers) {
    // Apply heuristic on row 0 (producer always uses row 0)
    uint64_t* bitmap_row = &bitmap->data[0 * bitmap->words_per_row];
    
    // Step 1: Clear bits that consumers are predicted to use
    // Heuristic: Set next num_consumers positions as UNSAFE (bit=0)
    int cleared_count = 0;
    int i = found_index + 1;

    while (cleared_count < num_consumers && i < bitmap->cols) {
        if (check_bit(bitmap_row, i)) {  // Chỉ clear nếu bit = 1
            clear_bit(bitmap_row, i);
            cleared_count++;
        }
        i++;
    }

    // Step 2: Set all bits AFTER the cleared range to 1 (available for producer)
    int clear_end = i;
    
    // Step 2: Set all bits AFTER the cleared range to 1 (available for producer)
    // OPTIMIZED: Set entire words at once instead of individual bits
    // IMPORTANT: BITMAP from shared memory may be all 0s (from calloc)
    // We need to mark positions after the predicted consumer range as available
    
    if (clear_end >= bitmap->cols) {
        return; // Nothing to set
    }
    
    int start_word = clear_end / 64;
    int start_bit = clear_end % 64;
    int end_word = (bitmap->cols - 1) / 64;
    
    // Handle first partial word (if start_bit != 0)
    if (start_bit != 0) {
        // Create mask for bits from start_bit to end of word
        uint64_t mask = ~((1ULL << start_bit) - 1);
        bitmap_row[start_word] |= mask;
        start_word++; // Move to next word
    }
    
    // Set all complete words to 0xFFFFFFFFFFFFFFFF
    for (int word = start_word; word < end_word; word++) {
        bitmap_row[word] = 0xFFFFFFFFFFFFFFFFULL;
    }
    
    // Handle last partial word (if needed)
    if (start_word <= end_word) {
        int last_bit = (bitmap->cols - 1) % 64;
        if (start_word == end_word && start_bit != 0) {
            // Already handled in first partial word
        } else {
            // Set bits from 0 to last_bit in the last word
            uint64_t mask = (1ULL << (last_bit + 1)) - 1;
            bitmap_row[end_word] |= mask;
        }
    }
}