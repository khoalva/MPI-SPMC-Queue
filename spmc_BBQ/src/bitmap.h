#ifndef BITMAP_H
#define BITMAP_H

#include <stdint.h>
#include <stdbool.h>

// Forward declaration for spmc_queue_t to avoid circular dependency
struct spmc_queue;

/**
 * @brief Bitmap structure for efficient bit operations
 */
typedef struct {
    int rows, cols;
    int words_per_row;
    uint64_t *data;
} bitmap_t;

// Basic bitmap operations
void bitmap_init(bitmap_t* map, int rows, int cols);
void bitmap_destroy(bitmap_t* map);

// Bit manipulation functions
int check_bit(uint64_t *row, int col);
void set_bit(uint64_t *row, int col);
void clear_bit(uint64_t *row, int col);

// Bitmap initialization functions
void set_all_bits(bitmap_t* bitmap, int row);
void set_all_bits_full(bitmap_t* bitmap);

// Search functions
int find_safe_index_from(int start, bitmap_t* sync_bitmap, int row);
int find_Nth_safe_index(int N, bitmap_t* sync_bitmap, int row, int last_index, int last_N);
int find_nth_set_bit_in_word(uint64_t word, int n);

// Bitmap synchronization and heuristics
void sync_bitmap_row(struct spmc_queue *queue, int row, bitmap_t* local_bitmap);
void heuristic_bitmap(bitmap_t* bitmap, int found_index, int num_consumers);

// Debug and printing functions
void print_bitmap(bitmap_t* bitmap, int max_rows, int max_cols, const char* title);
void print_queue_bitmaps(struct spmc_queue *queue, int max_rows, int max_cols);

#endif // BITMAP_H