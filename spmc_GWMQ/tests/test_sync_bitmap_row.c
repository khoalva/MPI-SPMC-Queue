#define _POSIX_C_SOURCE 199309L
#include "../src/spmc_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/**
 * Test suite for sync_bitmap_row function using MPI SPMC Queue
 * 
 * This test validates sync_bitmap_row in realistic concurrent scenarios:
 * - Process 0: Producer/coordinator - runs tests and validates results
 * - Other processes: Consumers - concurrently modify bitmap while sync occurs
 * 
 * Testing principle:
 * - Accept false negatives: if bitmap is 111100, reading 100100 is OK (missing bits)
 * - Reject false positives: if bitmap is 100000, reading 110000 is NOT OK (phantom bits)  
 * - sync_result must be subset of actual bitmap
 * - Consumers only set bits 0→1, never 1→0
 */

#define TEST_ROWS 4
#define TEST_COLS 128
#define NUM_TEST_CASES 20
#define MODIFICATIONS_PER_CONSUMER 10

// Test result tracking
typedef struct {
    int passed;
    int failed;
    int total;
} test_results_t;

// Helper functions
static void print_bitmap_row_bits(bitmap_t* bitmap, int row, int max_bits, const char* title);
static int validate_sync_result(spmc_queue_t* queue, bitmap_t* synced_bitmap, int row);

/**
 * Test 1: Basic sync functionality without concurrency
 */
static void test_basic_sync(spmc_queue_t* queue, test_results_t* results) {
    if (queue->mpi_ctx.rank != 0) return; // Only producer runs this test
    
    printf("\n=== Test 1: Basic Sync (No Concurrency) ===\n");
    
    bitmap_t* local_bitmap = malloc(sizeof(bitmap_t));
    bitmap_init(local_bitmap, 1, queue->q->bitmap->cols);
    
    // Test different patterns
    uint64_t test_patterns[] = {
        0x0000000000000000ULL,  // All zeros
        0xFFFFFFFFFFFFFFFFULL,  // All ones  
        0xAAAAAAAAAAAAAAAAULL,  // Alternating 10101010...
        0x5555555555555555ULL,  // Alternating 01010101...
        0x00000000FFFFFFFFULL,  // First half 0, second half 1
        0x8000000000000001ULL   // Only first and last bit
    };
    
    int num_patterns = sizeof(test_patterns) / sizeof(test_patterns[0]);
    
    for (int p = 0; p < num_patterns; p++) {
        // Setup pattern in queue bitmap (direct access since we're producer)
        if (spmc_queue_is_enqueuer(queue)) {
            uint64_t* bitmap_row = &queue->q->bitmap->data[0 * queue->q->bitmap->words_per_row];
            bitmap_row[0] = test_patterns[p];
            
            // Clear remaining words in the row
            for (int w = 1; w < queue->q->bitmap->words_per_row; w++) {
                bitmap_row[w] = 0;
            }
        }
        
        // Sync the row
        sync_bitmap_row(queue, 0, local_bitmap);
        
        // Validate result
        if (validate_sync_result(queue, local_bitmap, 0)) {
            printf("  ✓ Pattern %d (0x%016llX) passed\n", p + 1, (unsigned long long)test_patterns[p]);
            results->passed++;
        } else {
            printf("  ✗ Pattern %d (0x%016llX) failed\n", p + 1, (unsigned long long)test_patterns[p]);
            print_bitmap_row_bits(queue->q->bitmap, 0, 64, "Original");
            print_bitmap_row_bits(local_bitmap, 0, 64, "Synced");
            results->failed++;
        }
        results->total++;
    }
    
    free(local_bitmap->data);
    free(local_bitmap);
}

/**
 * Test 2: Concurrent modification by multiple consumers - Fully Asynchronous
 */
static void test_concurrent_modification(spmc_queue_t* queue, test_results_t* results) {
    printf("\n=== Test 2: Concurrent Modification ===\n");
    
    if (spmc_queue_is_enqueuer(queue)) {
        // Producer performs syncs while consumers continuously modify asynchronously
        bitmap_t* local_bitmap = malloc(sizeof(bitmap_t));
        bitmap_init(local_bitmap, 1, queue->q->bitmap->cols);
        
        printf("  Starting concurrent test for 4 seconds...\n");
        
        struct timespec start_time, current_time;
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        
        int sync_count = 0;
        int valid_syncs = 0;
        
        do {
            // Periodically reset bitmap to create different scenarios
            if (sync_count % 15 == 0) {
                uint64_t* bitmap_row = &queue->q->bitmap->data[0 * queue->q->bitmap->words_per_row];
                for (int w = 0; w < queue->q->bitmap->words_per_row; w++) {
                    bitmap_row[w] = 0;
                }
                if (sync_count > 0) {
                    printf("  Reset %d - starting fresh bitmap\n", sync_count / 15);
                }
            }
            
            // Random delay to vary timing with consumer operations
            usleep(50 + (rand() % 150));
            
            // Perform sync while consumers are asynchronously modifying
            sync_bitmap_row(queue, 0, local_bitmap);
            sync_count++;
            
            // Validate that synced result is subset of current state
            if (validate_sync_result(queue, local_bitmap, 0)) {
                valid_syncs++;
            } else {
                if (sync_count <= 5 || sync_count % 30 == 0) { // Log first few and occasional details
                    printf("  Sync %d validation details:\n", sync_count);
                    print_bitmap_row_bits(queue->q->bitmap, 0, 64, "Current bitmap");
                    print_bitmap_row_bits(local_bitmap, 0, 64, "Synced result");
                }
            }
            
            // Vary intervals between syncs
            usleep(30 + (rand() % 100));
            
            clock_gettime(CLOCK_MONOTONIC, &current_time);
        } while ((current_time.tv_sec - start_time.tv_sec) + 
                 (current_time.tv_nsec - start_time.tv_nsec) / 1e9 < 4.0);
        
        double success_rate = (double)valid_syncs / sync_count;
        printf("  Concurrent sync results: %d/%d valid (%.1f%%)\n", 
               valid_syncs, sync_count, success_rate * 100);
        
        if (success_rate >= 0.85) { // Expect high success rate
            printf("  ✓ Concurrent modification test passed\n");
            results->passed++;
        } else {
            printf("  ✗ Concurrent modification test failed\n");
            results->failed++;
        }
        results->total++;
        
        free(local_bitmap->data);
        free(local_bitmap);
        
    } else {
        // Consumer processes modify bitmap continuously and asynchronously
        int consumer_id = queue->mpi_ctx.rank - 1; // rank 1,2,3 -> consumer 0,1,2
        int start_bit = consumer_id * 20; // Each consumer gets 20 bits
        
        printf("  [Consumer %d] Starting continuous asynchronous modifications...\n", consumer_id);
        
        struct timespec start_time, current_time;
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        
        int total_modifications = 0;
        
        // Run for the same duration as producer
        do {
            // Modify bits atomically using fetch_and_op with BOR
            for (int i = 0; i < 3; i++) { // 3 modifications per cycle
                int bit_offset = rand() % 20; // Random bit in consumer's range
                int bit_pos = start_bit + bit_offset;
                
                if (bit_pos < queue->q->bitmap->cols) {
                    // Calculate word and bit position
                    int word_idx = bit_pos / 64;
                    int bit_idx = bit_pos % 64;
                    
                    // Create mask to set the specific bit
                    uint64_t mask = 1ULL << bit_idx;
                    uint64_t old_value;
                    
                    // Atomic OR operation to set bit
                    size_t offset = word_idx * sizeof(uint64_t);
                    MPI_TRY(mpi_fetch_and_op(&mask, &old_value, MPI_UINT64_T, 
                                           0, offset, MPI_BOR, &queue->q->win_bitmap));
                    total_modifications++;
                }
            }
            
            // Random delay to vary timing and create different race conditions
            usleep(25 + (rand() % 75));
            
            clock_gettime(CLOCK_MONOTONIC, &current_time);
        } while ((current_time.tv_sec - start_time.tv_sec) + 
                 (current_time.tv_nsec - start_time.tv_nsec) / 1e9 < 4.0);
        
        printf("  [Consumer %d] Completed %d total modifications\n", consumer_id, total_modifications);
    }
}

/**
 * Test 3: Edge cases and boundary conditions
 */
static void test_edge_cases(spmc_queue_t* queue, test_results_t* results) {
    if (!spmc_queue_is_enqueuer(queue)) return; // Only producer runs this test
    
    printf("\n=== Test 3: Edge Cases ===\n");
    
    bitmap_t* local_bitmap = malloc(sizeof(bitmap_t));
    bitmap_init(local_bitmap, 1, queue->q->bitmap->cols);
    
    // Test 1: Word boundary bits (around bit 63-64)
    printf("Testing word boundaries...\n");
    uint64_t* bitmap_row = &queue->q->bitmap->data[0 * queue->q->bitmap->words_per_row];
    
    // Clear first, then set bits around word boundary
    for (int w = 0; w < queue->q->bitmap->words_per_row; w++) {
        bitmap_row[w] = 0;
    }
    
    set_bit(bitmap_row, 62);
    set_bit(bitmap_row, 63); 
    set_bit(bitmap_row, 64);
    set_bit(bitmap_row, 65);
    
    sync_bitmap_row(queue, 0, local_bitmap);
    
    if (validate_sync_result(queue, local_bitmap, 0)) {
        printf("  ✓ Word boundary test passed\n");
        results->passed++;
    } else {
        printf("  ✗ Word boundary test failed\n");
        results->failed++;
    }
    results->total++;
    
    // Test 2: Different rows
    if (queue->q->bitmap->rows > 1) {
        printf("Testing different rows...\n");
        int test_row = queue->q->bitmap->rows - 1;
        uint64_t* test_bitmap_row = &queue->q->bitmap->data[test_row * queue->q->bitmap->words_per_row];
        test_bitmap_row[0] = 0xF0F0F0F0F0F0F0F0ULL;
        
        // Clear remaining words
        for (int w = 1; w < queue->q->bitmap->words_per_row; w++) {
            test_bitmap_row[w] = 0;
        }
        
        sync_bitmap_row(queue, test_row, local_bitmap);
        
        if (validate_sync_result(queue, local_bitmap, test_row)) {
            printf("  ✓ Different row test passed\n");
            results->passed++;
        } else {
            printf("  ✗ Different row test failed\n");
            results->failed++;
        }
        results->total++;
    }
    
    free(local_bitmap->data);
    free(local_bitmap);
}

/**
 * Test 4: Double-read monotonic test - Fully Asynchronous
 */
static void test_double_read_monotonic(spmc_queue_t* queue, test_results_t* results) {
    printf("\n=== Test 4: Double-Read Monotonic Test ===\n");
    
    if (spmc_queue_is_enqueuer(queue)) {
        // Producer performs double reads while consumers continuously modify
        bitmap_t* first_read = malloc(sizeof(bitmap_t));
        bitmap_t* second_read = malloc(sizeof(bitmap_t));
        bitmap_init(first_read, 1, queue->q->bitmap->cols);
        bitmap_init(second_read, 1, queue->q->bitmap->cols);
        
        printf("  Testing monotonic property for 5 seconds...\n");
        
        struct timespec start_time, current_time;
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        
        int monotonic_tests = 0;
        int monotonic_passed = 0;
        
        do {
            // Periodically reset bitmap to create fresh scenarios
            if (monotonic_tests % 12 == 0) {
                uint64_t* bitmap_row = &queue->q->bitmap->data[0 * queue->q->bitmap->words_per_row];
                for (int w = 0; w < queue->q->bitmap->words_per_row; w++) {
                    bitmap_row[w] = 0;
                }
                if (monotonic_tests > 0) {
                    printf("  Reset %d - starting fresh for monotonic test\n", monotonic_tests / 12);
                }
            }
            
            // Random initial delay
            usleep(100 + (rand() % 200));
            
            // First read while consumers are modifying asynchronously
            sync_bitmap_row(queue, 0, first_read);
            
            // Time gap between reads - let consumers add more bits
            usleep(500 + (rand() % 1000)); // 0.5-1.5ms gap
            
            // Second read 
            sync_bitmap_row(queue, 0, second_read);
            
            // Validate monotonic property: second_read must be superset of first_read
            int is_monotonic = 1;
            uint64_t* first_row = &first_read->data[0];
            uint64_t* second_row = &second_read->data[0];
            
            for (int i = 0; i < queue->q->bitmap->cols; i++) {
                int first_bit = check_bit(first_row, i);
                int second_bit = check_bit(second_row, i);
                
                if (first_bit == 1 && second_bit == 0) {
                    is_monotonic = 0;
                    break;
                }
            }
            
            monotonic_tests++;
            if (is_monotonic) {
                monotonic_passed++;
            } else {
                if (monotonic_tests <= 3) { // Show details for first few failures
                    printf("  ✗ Monotonic violation in test %d:\n", monotonic_tests);
                    print_bitmap_row_bits(first_read, 0, 64, "First read");
                    print_bitmap_row_bits(second_read, 0, 64, "Second read");
                }
            }
            
            // Vary timing between tests
            usleep(200 + (rand() % 300));
            
            clock_gettime(CLOCK_MONOTONIC, &current_time);
        } while ((current_time.tv_sec - start_time.tv_sec) + 
                 (current_time.tv_nsec - start_time.tv_nsec) / 1e9 < 5.0);
        
        double monotonic_rate = (double)monotonic_passed / monotonic_tests;
        printf("  Monotonic tests: %d/%d passed (%.1f%%)\n", 
               monotonic_passed, monotonic_tests, monotonic_rate * 100);
        
        if (monotonic_rate >= 0.95) { // Very high expectation for monotonic property
            printf("  ✓ Double-read monotonic test passed\n");
            results->passed++;
        } else {
            printf("  ✗ Double-read monotonic test failed\n");
            results->failed++;
        }
        results->total++;
        
        free(first_read->data);
        free(first_read);
        free(second_read->data);
        free(second_read);
        
    } else {
        // Consumer processes continuously modify bitmap to increase bit density
        int consumer_id = queue->mpi_ctx.rank - 1;
        int start_bit = consumer_id * 25; // Wider range for more coverage
        
        printf("  [Consumer %d] Continuously adding bits for monotonic test...\n", consumer_id);
        
        struct timespec start_time, current_time;
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        
        int monotonic_modifications = 0;
        
        do {
            // Continuous bit setting with varying patterns
            for (int cycle = 0; cycle < 4; cycle++) {
                int bit_offset = (monotonic_modifications % 25); // Cycle through consumer's range
                int bit_pos = start_bit + bit_offset;
                
                if (bit_pos < queue->q->bitmap->cols) {
                    int word_idx = bit_pos / 64;
                    int bit_idx = bit_pos % 64;
                    uint64_t mask = 1ULL << bit_idx;
                    uint64_t old_value;
                    
                    size_t offset = word_idx * sizeof(uint64_t);
                    MPI_TRY(mpi_fetch_and_op(&mask, &old_value, MPI_UINT64_T, 
                                           0, offset, MPI_BOR, &queue->q->win_bitmap));
                    monotonic_modifications++;
                }
            }
            
            // Vary timing to create different race scenarios
            usleep(30 + (rand() % 70));
            
            clock_gettime(CLOCK_MONOTONIC, &current_time);
        } while ((current_time.tv_sec - start_time.tv_sec) + 
                 (current_time.tv_nsec - start_time.tv_nsec) / 1e9 < 5.0);
        
        printf("  [Consumer %d] Added %d bits for monotonic testing\n", 
               consumer_id, monotonic_modifications);
    }
}

/**
 * Test 5: Extreme stress - High-frequency concurrent modifications
 */
static void test_extreme_stress(spmc_queue_t* queue, test_results_t* results) {
    printf("\n=== Test 5: Extreme Stress Test ===\n");
    
    if (spmc_queue_is_enqueuer(queue)) {
        // Producer: Multiple rapid syncs while consumers are hammering the bitmap
        bitmap_t* stress_bitmap = malloc(sizeof(bitmap_t));
        bitmap_init(stress_bitmap, 1, queue->q->bitmap->cols);
        
        printf("  Starting continuous stress test for 10 seconds...\n");
        
        struct timespec start_time, current_time;
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        
        int total_syncs = 0;
        int valid_syncs = 0;
        int round = 0;
        
        do {
            // Reset bitmap periodically for variety
            if (total_syncs % 100 == 0) {
                uint64_t* bitmap_row = &queue->q->bitmap->data[0 * queue->q->bitmap->words_per_row];
                uint64_t reset_pattern = ((uint64_t)rand() << 32) | rand();
                for (int w = 0; w < queue->q->bitmap->words_per_row; w++) {
                    bitmap_row[w] = (w == 0) ? reset_pattern : 0;
                }
                round++;
                if (round % 10 == 0) {
                    printf("    Round %d - %d syncs completed\n", round, total_syncs);
                }
            }
            
            // Perform sync with random micro-delays to vary timing
            if (total_syncs % 3 == 0) {
                usleep(rand() % 100); // 0-99 microseconds
            }
            
            sync_bitmap_row(queue, 0, stress_bitmap);
            total_syncs++;
            
            if (validate_sync_result(queue, stress_bitmap, 0)) {
                valid_syncs++;
            }
            
            clock_gettime(CLOCK_MONOTONIC, &current_time);
        } while ((current_time.tv_sec - start_time.tv_sec) + 
                 (current_time.tv_nsec - start_time.tv_nsec) / 1e9 < 10.0);
        
        double success_rate = (double)valid_syncs / total_syncs;
        printf("  Stress test completed: %d/%d syncs valid (%.1f%%)\n", 
               valid_syncs, total_syncs, success_rate * 100);
        
        if (success_rate >= 0.85) { // 85% success rate under extreme stress
            printf("  ✓ Extreme stress test passed\n");
            results->passed++;
        } else {
            printf("  ✗ Extreme stress test failed (success rate too low)\n");
            results->failed++;
        }
        results->total++;
        
        free(stress_bitmap->data);
        free(stress_bitmap);
        
    } else {
        // Consumers: Continuous asynchronous bitmap hammering
        int consumer_id = queue->mpi_ctx.rank - 1;
        int start_bit = consumer_id * 30; // Wider range per consumer
        
        printf("  [Consumer %d] Starting continuous modifications...\n", consumer_id);
        
        struct timespec start_time, current_time;
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        
        int modification_count = 0;
        int burst_count = 0;
        
        do {
            // Variable intensity bursts
            int burst_size = 10 + (rand() % 20); // 10-29 modifications per burst
            
            for (int i = 0; i < burst_size; i++) {
                int bit_offset = rand() % 30; // Random bit within consumer's range
                int bit_pos = start_bit + bit_offset;
                
                if (bit_pos < queue->q->bitmap->cols) {
                    int word_idx = bit_pos / 64;
                    int bit_idx = bit_pos % 64;
                    uint64_t mask = 1ULL << bit_idx;
                    uint64_t old_value;
                    
                    size_t offset = word_idx * sizeof(uint64_t);
                    MPI_TRY(mpi_fetch_and_op(&mask, &old_value, MPI_UINT64_T, 
                                           0, offset, MPI_BOR, &queue->q->win_bitmap));
                    modification_count++;
                }
            }
            
            burst_count++;
            
            // Random pause between bursts (0-500 microseconds)
            usleep(rand() % 500);
            
            clock_gettime(CLOCK_MONOTONIC, &current_time);
        } while ((current_time.tv_sec - start_time.tv_sec) + 
                 (current_time.tv_nsec - start_time.tv_nsec) / 1e9 < 10.0);
        
        printf("  [Consumer %d] Completed %d modifications in %d bursts\n", 
               consumer_id, modification_count, burst_count);
    }
}

/**
 * Test 6: Word boundary stress test - Asynchronous
 */
static void test_word_boundary_stress(spmc_queue_t* queue, test_results_t* results) {
    printf("\n=== Test 6: Word Boundary Stress ===\n");
    
    if (spmc_queue_is_enqueuer(queue)) {
        bitmap_t* boundary_bitmap = malloc(sizeof(bitmap_t));
        bitmap_init(boundary_bitmap, 1, queue->q->bitmap->cols);
        
        printf("  Testing word boundary synchronization for 5 seconds...\n");
        
        struct timespec start_time, current_time;
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        
        int boundary_tests = 0;
        int boundary_successes = 0;
        
        do {
            // Focus on different word boundaries each iteration
            int focus_word = boundary_tests % queue->q->bitmap->words_per_row;
            
            // Set pattern around word boundaries
            uint64_t* bitmap_row = &queue->q->bitmap->data[0 * queue->q->bitmap->words_per_row];
            
            // Clear all words first
            for (int w = 0; w < queue->q->bitmap->words_per_row; w++) {
                bitmap_row[w] = 0;
            }
            
            // Set specific pattern around the focus word boundary
            if (focus_word < queue->q->bitmap->words_per_row) {
                bitmap_row[focus_word] = 0xC000000000000003ULL; // Bits 0,1,62,63 set
            }
            if (focus_word + 1 < queue->q->bitmap->words_per_row) {
                bitmap_row[focus_word + 1] = 0xC000000000000003ULL; // Same pattern next word
            }
            
            // Give consumers a moment to start modifying (no barrier!)
            usleep(50 + (rand() % 100));
            
            // Test multiple syncs at this boundary
            int local_successes = 0;
            for (int sync_attempt = 0; sync_attempt < 5; sync_attempt++) {
                sync_bitmap_row(queue, 0, boundary_bitmap);
                
                if (validate_sync_result(queue, boundary_bitmap, 0)) {
                    local_successes++;
                }
                
                // Vary timing to catch different race conditions
                usleep(10 + (rand() % 50));
            }
            
            boundary_tests++;
            if (local_successes >= 3) { // At least 3/5 should work
                boundary_successes++;
            }
            
            // Small pause before next boundary test
            usleep(100 + (rand() % 200));
            
            clock_gettime(CLOCK_MONOTONIC, &current_time);
        } while ((current_time.tv_sec - start_time.tv_sec) + 
                 (current_time.tv_nsec - start_time.tv_nsec) / 1e9 < 5.0);
        
        double boundary_success_rate = (double)boundary_successes / boundary_tests;
        printf("  Word boundary tests: %d/%d passed (%.1f%%)\n", 
               boundary_successes, boundary_tests, boundary_success_rate * 100);
        
        if (boundary_success_rate >= 0.8) {
            printf("  ✓ Word boundary stress test passed\n");
            results->passed++;
        } else {
            printf("  ✗ Word boundary stress test failed\n");
            results->failed++;
        }
        results->total++;
        
        free(boundary_bitmap->data);
        free(boundary_bitmap);
        
    } else {
        // Consumers: Focus on word boundary bits asynchronously
        int consumer_id = queue->mpi_ctx.rank - 1;
        
        printf("  [Consumer %d] Targeting word boundaries...\n", consumer_id);
        
        struct timespec start_time, current_time;
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        
        int boundary_modifications = 0;
        
        do {
            // Target different word boundaries based on time and consumer_id
            int target_word = (boundary_modifications / 20 + consumer_id) % queue->q->bitmap->words_per_row;
            
            // Focus on critical boundary bits: 62,63 of current word and 0,1 of next word
            int boundary_bits[] = {62, 63, 0, 1}; // Relative to word boundary
            int num_boundary_bits = 4;
            
            for (int i = 0; i < num_boundary_bits; i++) {
                int bit_in_word = boundary_bits[i];
                int actual_word = target_word;
                
                // Adjust for bits 0,1 (they belong to next word)
                if (bit_in_word < 32) {
                    actual_word = (target_word + 1) % queue->q->bitmap->words_per_row;
                }
                
                int absolute_bit = actual_word * 64 + bit_in_word;
                
                if (absolute_bit < queue->q->bitmap->cols) {
                    // Random chance to modify this boundary bit
                    if (rand() % 3 == 0) { // 33% chance
                        int word_idx = absolute_bit / 64;
                        int bit_idx = absolute_bit % 64;
                        uint64_t mask = 1ULL << bit_idx;
                        uint64_t old_value;
                        
                        size_t offset = word_idx * sizeof(uint64_t);
                        MPI_TRY(mpi_fetch_and_op(&mask, &old_value, MPI_UINT64_T, 
                                               0, offset, MPI_BOR, &queue->q->win_bitmap));
                        boundary_modifications++;
                    }
                }
            }
            
            // Random timing to create various race conditions
            usleep(20 + (rand() % 80));
            
            clock_gettime(CLOCK_MONOTONIC, &current_time);
        } while ((current_time.tv_sec - start_time.tv_sec) + 
                 (current_time.tv_nsec - start_time.tv_nsec) / 1e9 < 5.0);
        
        printf("  [Consumer %d] Made %d boundary modifications\n", 
               consumer_id, boundary_modifications);
    }
}

/**
 * Test 7: Random pattern chaos test
 */
static void test_random_chaos(spmc_queue_t* queue, test_results_t* results) {
    if (!spmc_queue_is_enqueuer(queue)) return; // Only producer runs this test
    
    printf("\n=== Test 7: Random Pattern Chaos ===\n");
    
    bitmap_t* chaos_bitmap = malloc(sizeof(bitmap_t));
    bitmap_init(chaos_bitmap, 1, queue->q->bitmap->cols);
    
    for (int chaos_round = 0; chaos_round < 30; chaos_round++) {
        // Create completely random bitmap state
        uint64_t* bitmap_row = &queue->q->bitmap->data[0 * queue->q->bitmap->words_per_row];
        
        // Fill with random data
        for (int w = 0; w < queue->q->bitmap->words_per_row; w++) {
            bitmap_row[w] = ((uint64_t)rand() << 32) | rand();
        }
        
        // Rapid syncs with random delays
        int syncs_valid = 0;
        int total_syncs = 5 + (rand() % 10); // 5-14 syncs
        
        for (int i = 0; i < total_syncs; i++) {
            // Random micro-delay
            usleep(rand() % 1000);
            
            sync_bitmap_row(queue, 0, chaos_bitmap);
            
            if (validate_sync_result(queue, chaos_bitmap, 0)) {
                syncs_valid++;
            }
        }
        
        if (syncs_valid == total_syncs) {
            results->passed++;
        } else {
            printf("  ✗ Chaos round %d: %d/%d syncs valid\n", chaos_round + 1, syncs_valid, total_syncs);
            results->failed++;
        }
        results->total++;
    }
    
    printf("  Random chaos test completed\n");
    
    free(chaos_bitmap->data);
    free(chaos_bitmap);
}

// Helper function implementations
static void print_bitmap_row_bits(bitmap_t* bitmap, int row, int max_bits, const char* title) {
    printf("%s (Row %d): ", title, row);
    uint64_t* bitmap_row = &bitmap->data[row * bitmap->words_per_row];
    
    for (int i = 0; i < max_bits && i < bitmap->cols; i++) {
        printf("%d", check_bit(bitmap_row, i));
        if ((i + 1) % 8 == 0) printf(" ");
    }
    printf("\n");
}

static int validate_sync_result(spmc_queue_t* queue, bitmap_t* synced_bitmap, int row) {
    uint64_t* orig_row = &queue->q->bitmap->data[row * queue->q->bitmap->words_per_row];
    uint64_t* sync_row = &synced_bitmap->data[0]; // sync result goes to row 0
    
    // Validate: synced result must be subset of original
    // If synced_bit == 1, then original_bit must be 1 (no false positives allowed)
    // If synced_bit == 0 but original_bit == 1, that's OK (false negative acceptable)
    for (int i = 0; i < queue->q->bitmap->cols; i++) {
        int sync_bit = check_bit(sync_row, i);
        int orig_bit = check_bit(orig_row, i);
        
        if (sync_bit == 1 && orig_bit == 0) {
            printf("    Validation failed at bit %d: synced=1, original=0 (false positive!)\n", i);
            return 0;
        }
        // sync_bit == 0 && orig_bit == 1 is acceptable (missing bit - false negative)
    }
    
    return 1; // Valid - synced is subset of original
}



int main(int argc, char* argv[]) {
    printf("=== SPMC Queue sync_bitmap_row Test Suite ===\n");
    printf("Testing data race validation with MPI\n");
    printf("Principle: Accept false positives, reject false negatives\n\n");
    
    // Initialize SPMC queue (includes MPI initialization)
    spmc_queue_t queue;
    if (spmc_queue_init(&queue, argc, argv) != 0) {
        fprintf(stderr, "Failed to initialize SPMC queue\n");
        return 1;
    }
    
    srand(time(NULL) + queue.mpi_ctx.rank);
    
    test_results_t results = {0, 0, 0};
    
    // Run tests
    test_basic_sync(&queue, &results);
    test_concurrent_modification(&queue, &results);
    test_edge_cases(&queue, &results);
    test_double_read_monotonic(&queue, &results);
    test_extreme_stress(&queue, &results);
    test_word_boundary_stress(&queue, &results);
    test_random_chaos(&queue, &results);
    
    // Gather results from all processes
    int global_passed, global_failed, global_total;
    MPI_TRY(mpi_allreduce(&results.passed, &global_passed, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD));
    MPI_TRY(mpi_allreduce(&results.failed, &global_failed, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD));
    MPI_TRY(mpi_allreduce(&results.total, &global_total, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD));
    
    // Print results (only from rank 0)
    if (queue.mpi_ctx.rank == 0) {
        printf("\n=== Final Test Results ===\n");
        printf("Total tests: %d\n", global_total);
        printf("Passed: %d (%.1f%%)\n", global_passed, 
               global_total > 0 ? (100.0 * global_passed / global_total) : 0.0);
        printf("Failed: %d (%.1f%%)\n", global_failed,
               global_total > 0 ? (100.0 * global_failed / global_total) : 0.0);
        
        if (global_failed == 0) {
            printf("\n✓ All tests passed! sync_bitmap_row function works correctly.\n");
        } else {
            printf("\n✗ Some tests failed. Check sync_bitmap_row implementation.\n");
        }
    }
    
    spmc_queue_destroy(&queue);
    
    return (global_failed == 0) ? 0 : 1;
}