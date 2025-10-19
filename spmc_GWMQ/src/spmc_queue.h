#ifndef SPMC_QUEUE_H
#define SPMC_QUEUE_H

#include <stdbool.h>
#include <stdint.h>
#include "mpi_lib.h"

#define L -1 // ⊥ (empty cell)
#define T -2 // ⊤ (dequeued cell)
#define MAX_QUEUE_SIZE 1024
#define MAX_ROW 1024

// Use uint64_t for atomic operations with MPI_Compare_and_swap
// Pack: lower 32 bits = data, upper 32 bits = gen
typedef uint64_t cell_t;

// Helper macros for cell operations
#define PACK_CELL(data, gen) (((uint64_t)(uint32_t)(data)) | (((uint64_t)(uint32_t)(gen)) << 32))
#define GET_DATA(cell) ((int)(uint32_t)((cell) & 0xFFFFFFFFULL))
#define GET_GEN(cell) ((int)(uint32_t)((cell) >> 32))
#define MAKE_CELL(data, gen) PACK_CELL(data, gen)

typedef struct{
    int rows, cols;
    int words_per_row;
    uint64_t *data;
} bitmap_t;

typedef struct{
    int tail;
    int enq_row;
    int size;
    bitmap_t* map;
} producer_t;

typedef struct{
    int size;
    int last_deq_row;
    int last_value;
    bitmap_t* map;
} consumer_t;



typedef struct{
    cell_t* items;
    int* heads;
    int row;
    bitmap_t* bitmap;
    bitmap_t* sync_bitmap;

    mpi_window_t win_items;
    mpi_window_t win_heads;
    mpi_window_t win_row;
    mpi_window_t win_bitmap;
    mpi_window_t win_sync_bitmap;
} structure_t;

typedef struct {
    mpi_context_t mpi_ctx; // Must have MPI context for communication

    producer_t* p;
    consumer_t* c;
    structure_t* q;

} spmc_queue_t;



// Must have for benchmark compatibility
int spmc_queue_init(spmc_queue_t *queue, int argc, char *argv[]);
void spmc_queue_destroy(spmc_queue_t *queue);
int spmc_queue_enqueue(spmc_queue_t *queue, int value);
int spmc_queue_dequeue(spmc_queue_t *queue);
void spmc_queue_print_stats(spmc_queue_t *queue);
int spmc_queue_is_enqueuer(spmc_queue_t *queue);
size_t spmc_queue_get_capacity_bytes(const spmc_queue_t *queue);

// Can add more utility functions and structs as needed
void bitmap_init(bitmap_t *map, int rows, int words_per_row);
void bitmap_destroy(bitmap_t *map);
int check_bit(uint64_t *map, int col);
void set_bit(uint64_t *row, int col);
void sync_bitmap_row(spmc_queue_t *queue, int row, bitmap_t* local_bitmap);
void set_all_bits(bitmap_t* bitmap, int row) ;
void set_all_bits_full(bitmap_t* bitmap) ;
void print_queue_bitmaps(spmc_queue_t *queue, int max_rows, int max_cols);
void print_bitmap(bitmap_t* bitmap, int max_rows, int max_cols, const char* title) ;


int find_Nth_safe_index(int N, bitmap_t* sync_bitmap, int row);
int find_safe_index_from(int start, bitmap_t* sync_bitmap, int row);
int find_nth_set_bit_in_word(uint64_t word, int n) ;
#endif
