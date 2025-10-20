#define _POSIX_C_SOURCE 199309L
#include "../src/spmc_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <limits.h>

/**
 * Test utilities for bitmap functions
 */

// Helper function to setup test bitmap
bitmap_t* create_test_bitmap(int rows, int cols) {
    bitmap_t* map = malloc(sizeof(bitmap_t));
    bitmap_init(map, rows, cols);
    return map;
}

// Helper function to cleanup test bitmap
void destroy_test_bitmap(bitmap_t* map) {
    if (map) {
        free(map->data);
        free(map);
    }
}

// Helper function to set bits manually for testing
void setup_test_pattern(bitmap_t* bitmap, int row, int* set_positions, int num_positions) {
    // Clear all bits first
    uint64_t* bitmap_row = &bitmap->data[row * bitmap->words_per_row];
    for (int i = 0; i < bitmap->words_per_row; i++) {
        bitmap_row[i] = 0;
    }
    
    // Set specified bits
    for (int i = 0; i < num_positions; i++) {
        if (set_positions[i] < bitmap->cols) {
            set_bit(bitmap_row, set_positions[i]);
        }
    }
}

// Helper to print bitmap state for debugging
void print_test_bitmap_row(bitmap_t* bitmap, int row, const char* description) {
    printf("%s: Row %d: ", description, row);
    uint64_t* bitmap_row = &bitmap->data[row * bitmap->words_per_row];
    for (int i = 0; i < bitmap->cols && i < 128; i++) {  // Limit output for readability
        printf("%d", check_bit(bitmap_row, i));
        if ((i + 1) % 8 == 0) printf(" ");
    }
    if (bitmap->cols > 128) printf("...");
    printf("\n");
}

/**
 * Test Case 1: find_safe_index_from basic functionality
 */
void test_find_safe_index_from_basic() {
    printf("=== Test 1: find_safe_index_from basic functionality ===\n");
    
    bitmap_t* bitmap = create_test_bitmap(1, 64);
    
    // Test 1.1: Empty bitmap (all bits 0)
    int result = find_safe_index_from(0, bitmap, 0);
    printf("Test 1.1 - Empty bitmap: result=%d (expected: -1)\n", result);
    assert(result == -1);
    
    // Test 1.2: Single bit set at position 5
    int positions1[] = {5};
    setup_test_pattern(bitmap, 0, positions1, 1);
    print_test_bitmap_row(bitmap, 0, "Test 1.2");
    
    result = find_safe_index_from(0, bitmap, 0);
    printf("Test 1.2 - Single bit at 5, start from 0: result=%d (expected: 5)\n", result);
    assert(result == 5);
    
    result = find_safe_index_from(5, bitmap, 0);
    printf("Test 1.2 - Single bit at 5, start from 5: result=%d (expected: 5)\n", result);
    assert(result == 5);
    
    result = find_safe_index_from(6, bitmap, 0);
    printf("Test 1.2 - Single bit at 5, start from 6: result=%d (expected: 5, wrap around)\n", result);
    assert(result == 5);
    
    // Test 1.3: Multiple bits set
    int positions2[] = {1, 10, 25, 63};
    setup_test_pattern(bitmap, 0, positions2, 4);
    print_test_bitmap_row(bitmap, 0, "Test 1.3");
    
    result = find_safe_index_from(0, bitmap, 0);
    printf("Test 1.3 - Multiple bits, start from 0: result=%d (expected: 1)\n", result);
    assert(result == 1);
    
    result = find_safe_index_from(5, bitmap, 0);
    printf("Test 1.3 - Multiple bits, start from 5: result=%d (expected: 10)\n", result);
    assert(result == 10);
    
    result = find_safe_index_from(26, bitmap, 0);
    printf("Test 1.3 - Multiple bits, start from 26: result=%d (expected: 63)\n", result);
    assert(result == 63);
    
    result = find_safe_index_from(64, bitmap, 0);  // Start beyond bounds
    printf("Test 1.3 - Multiple bits, start from 64 (beyond bounds): result=%d (expected: 1, wrap around)\n", result);
    assert(result == 1);
    
    destroy_test_bitmap(bitmap);
    printf("Test 1 PASSED\n\n");
}

/**
 * Test Case 2: find_safe_index_from edge cases
 */
void test_find_safe_index_from_edge_cases() {
    printf("=== Test 2: find_safe_index_from edge cases ===\n");
    
    // Test 2.1: Large bitmap with sparse bits
    bitmap_t* bitmap = create_test_bitmap(1, 200);
    int positions[] = {0, 64, 128, 199};  // Cross word boundaries
    setup_test_pattern(bitmap, 0, positions, 4);
    print_test_bitmap_row(bitmap, 0, "Test 2.1");
    
    int result = find_safe_index_from(1, bitmap, 0);
    printf("Test 2.1 - Cross word boundaries, start from 1: result=%d (expected: 64)\n", result);
    assert(result == 64);
    
    result = find_safe_index_from(100, bitmap, 0);
    printf("Test 2.1 - Cross word boundaries, start from 100: result=%d (expected: 128)\n", result);
    assert(result == 128);
    
    result = find_safe_index_from(200, bitmap, 0);
    printf("Test 2.1 - Cross word boundaries, start from 200 (wrap): result=%d (expected: 0)\n", result);
    assert(result == 0);
    
    destroy_test_bitmap(bitmap);
    printf("Test 2 PASSED\n\n");
}

/**
 * Test Case 3: find_Nth_safe_index basic functionality
 */
void test_find_Nth_safe_index_basic() {
    printf("=== Test 3: find_Nth_safe_index basic functionality ===\n");
    
    bitmap_t* bitmap = create_test_bitmap(1, 64);
    
    // Test 3.1: Sequential bits
    int positions1[] = {5, 6, 7, 8, 9};
    setup_test_pattern(bitmap, 0, positions1, 5);
    print_test_bitmap_row(bitmap, 0, "Test 3.1");
    
    int result = find_Nth_safe_index(0, bitmap, 0, -1, -1);
    printf("Test 3.1 - N=0 (first): result=%d (expected: 5)\n", result);
    assert(result == 5);
    
    result = find_Nth_safe_index(1, bitmap, 0, -1, -1);
    printf("Test 3.1 - N=1 (second): result=%d (expected: 6)\n", result);
    assert(result == 6);
    
    result = find_Nth_safe_index(4, bitmap, 0, -1, -1);
    printf("Test 3.1 - N=4 (fifth): result=%d (expected: 9)\n", result);
    assert(result == 9);
    
    result = find_Nth_safe_index(5, bitmap, 0, -1, -1);
    printf("Test 3.1 - N=5 (doesn't exist): result=%d (expected: -1)\n", result);
    assert(result == -1);
    
    // Test 3.2: Sparse bits
    int positions2[] = {1, 10, 25, 40, 63};
    setup_test_pattern(bitmap, 0, positions2, 5);
    print_test_bitmap_row(bitmap, 0, "Test 3.2");
    
    result = find_Nth_safe_index(0, bitmap, 0, -1, -1);
    printf("Test 3.2 - N=0: result=%d (expected: 1)\n", result);
    assert(result == 1);
    
    result = find_Nth_safe_index(2, bitmap, 0, -1, -1);
    printf("Test 3.2 - N=2: result=%d (expected: 25)\n", result);
    assert(result == 25);
    
    result = find_Nth_safe_index(4, bitmap, 0, -1, -1);
    printf("Test 3.2 - N=4: result=%d (expected: 63)\n", result);
    assert(result == 63);
    
    destroy_test_bitmap(bitmap);
    printf("Test 3 PASSED\n\n");
}

/**
 * Test Case 4: find_Nth_safe_index optimization
 */
void test_find_Nth_safe_index_optimization() {
    printf("=== Test 4: find_Nth_safe_index optimization ===\n");
    
    bitmap_t* bitmap = create_test_bitmap(1, 128);
    
    // Set up pattern: bits at 10, 20, 30, 40, 50, 60, 70, 80, 90, 100
    int positions[] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
    setup_test_pattern(bitmap, 0, positions, 10);
    print_test_bitmap_row(bitmap, 0, "Test 4");
    
    // Test optimization: find N=2 first, then use that result to find N=5
    int result2 = find_Nth_safe_index(2, bitmap, 0, -1, -1);
    printf("Test 4 - N=2: result=%d (expected: 30)\n", result2);
    assert(result2 == 30);
    
    // Now use optimization to find N=5 (should start from after position 30)
    int result5 = find_Nth_safe_index(5, bitmap, 0, result2, 2);
    printf("Test 4 - N=5 with optimization (last_index=%d, last_N=2): result=%d (expected: 60)\n", 
           result2, result5);
    assert(result5 == 60);
    
    // Test that optimization gives same result as non-optimized
    int result5_no_opt = find_Nth_safe_index(5, bitmap, 0, -1, -1);
    printf("Test 4 - N=5 without optimization: result=%d\n", result5_no_opt);
    assert(result5 == result5_no_opt);
    
    // Test optimization with invalid parameters (should fallback)
    int result3 = find_Nth_safe_index(3, bitmap, 0, result5, 5);  // N < last_N
    printf("Test 4 - N=3 with invalid optimization: result=%d (expected: 40)\n", result3);
    assert(result3 == 40);
    
    destroy_test_bitmap(bitmap);
    printf("Test 4 PASSED\n\n");
}

/**
 * Test Case 5: Cross-word boundary tests
 */
void test_cross_word_boundary() {
    printf("=== Test 5: Cross-word boundary tests ===\n");
    
    bitmap_t* bitmap = create_test_bitmap(1, 256);
    
    // Set bits across multiple 64-bit words
    int positions[] = {0, 63, 64, 127, 128, 191, 192, 255};
    setup_test_pattern(bitmap, 0, positions, 8);
    print_test_bitmap_row(bitmap, 0, "Test 5");
    
    // Test find_safe_index_from across word boundaries
    int result = find_safe_index_from(62, bitmap, 0);
    printf("Test 5.1 - find_safe_index_from(62): result=%d (expected: 63)\n", result);
    assert(result == 63);
    
    result = find_safe_index_from(64, bitmap, 0);
    printf("Test 5.1 - find_safe_index_from(64): result=%d (expected: 64)\n", result);
    assert(result == 64);
    
    // Test find_Nth_safe_index across word boundaries
    result = find_Nth_safe_index(0, bitmap, 0, -1, -1);
    printf("Test 5.2 - find_Nth_safe_index(0): result=%d (expected: 0)\n", result);
    assert(result == 0);
    
    result = find_Nth_safe_index(2, bitmap, 0, -1, -1);
    printf("Test 5.2 - find_Nth_safe_index(2): result=%d (expected: 64)\n", result);
    assert(result == 64);
    
    result = find_Nth_safe_index(7, bitmap, 0, -1, -1);
    printf("Test 5.2 - find_Nth_safe_index(7): result=%d (expected: 255)\n", result);
    assert(result == 255);
    
    destroy_test_bitmap(bitmap);
    printf("Test 5 PASSED\n\n");
}

/**
 * Test Case 6: Stress test with large bitmap
 */
void test_stress_large_bitmap() {
    printf("=== Test 6: Stress test with large bitmap ===\n");
    
    bitmap_t* bitmap = create_test_bitmap(1, 1000);
    
    // Set every 7th bit as safe (to create irregular pattern)
    uint64_t* bitmap_row = &bitmap->data[0];
    for (int i = 0; i < bitmap->words_per_row; i++) {
        bitmap_row[i] = 0;
    }
    
    int expected_positions[200];
    int num_set = 0;
    for (int i = 0; i < 1000 && num_set < 200; i += 7) {
        set_bit(bitmap_row, i);
        expected_positions[num_set] = i;
        num_set++;
    }
    
    printf("Test 6 - Set %d bits at positions 0,7,14,21...\n", num_set);
    
    // Test find_safe_index_from with various start positions
    for (int start = 0; start < 50; start += 10) {
        int result = find_safe_index_from(start, bitmap, 0);
        
        // Find expected result manually
        int expected = -1;
        for (int i = start; i < 1000; i++) {
            if (i % 7 == 0) {
                expected = i;
                break;
            }
        }
        if (expected == -1) {  // Wrap around
            expected = 0;
        }
        
        printf("Test 6.1 - find_safe_index_from(%d): result=%d (expected: %d)\n", 
               start, result, expected);
        assert(result == expected);
    }
    
    // Test find_Nth_safe_index for various N values
    for (int N = 0; N < 10; N++) {
        int result = find_Nth_safe_index(N, bitmap, 0, -1, -1);
        int expected = (N < num_set) ? expected_positions[N] : -1;
        printf("Test 6.2 - find_Nth_safe_index(%d): result=%d (expected: %d)\n", 
               N, result, expected);
        assert(result == expected);
    }
    
    destroy_test_bitmap(bitmap);
    printf("Test 6 PASSED\n\n");
}

/**
 * Test Case 7: Multi-row bitmap test
 */
void test_multi_row_bitmap() {
    printf("=== Test 7: Multi-row bitmap test ===\n");
    
    bitmap_t* bitmap = create_test_bitmap(3, 64);
    
    // Set different patterns for different rows
    // Row 0: bits 1, 3, 5
    int row0_positions[] = {1, 3, 5};
    setup_test_pattern(bitmap, 0, row0_positions, 3);
    
    // Row 1: bits 10, 20, 30  
    int row1_positions[] = {10, 20, 30};
    setup_test_pattern(bitmap, 1, row1_positions, 3);
    
    // Row 2: bits 0, 63
    int row2_positions[] = {0, 63};
    setup_test_pattern(bitmap, 2, row2_positions, 2);
    
    for (int row = 0; row < 3; row++) {
        print_test_bitmap_row(bitmap, row, "Test 7");
    }
    
    // Test each row independently
    int result = find_safe_index_from(0, bitmap, 0);
    printf("Test 7 - Row 0, find_safe_index_from(0): result=%d (expected: 1)\n", result);
    assert(result == 1);
    
    result = find_Nth_safe_index(2, bitmap, 0, -1, -1);
    printf("Test 7 - Row 0, find_Nth_safe_index(2): result=%d (expected: 5)\n", result);
    assert(result == 5);
    
    result = find_safe_index_from(15, bitmap, 1);
    printf("Test 7 - Row 1, find_safe_index_from(15): result=%d (expected: 20)\n", result);
    assert(result == 20);
    
    result = find_Nth_safe_index(1, bitmap, 2, -1, -1);
    printf("Test 7 - Row 2, find_Nth_safe_index(1): result=%d (expected: 63)\n", result);
    assert(result == 63);
    
    destroy_test_bitmap(bitmap);
    printf("Test 7 PASSED\n\n");
}

int main() {
    printf("====================================\n");
    printf("Bitmap Functions Comprehensive Test\n");
    printf("====================================\n\n");
    
    // Run all test cases
    test_find_safe_index_from_basic();
    test_find_safe_index_from_edge_cases(); 
    test_find_Nth_safe_index_basic();
    test_find_Nth_safe_index_optimization();
    test_cross_word_boundary();
    test_stress_large_bitmap();
    test_multi_row_bitmap();
    
    printf("====================================\n");
    printf("ALL BITMAP TESTS PASSED! ✅\n");
    printf("====================================\n");
    
    return 0;
}