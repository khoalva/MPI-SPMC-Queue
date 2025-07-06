/**
 * @file test_mpi_lib.c
 * @brief Comprehensive test suite for MPI wrapper library
 * @version 1.0.0
 * 
 * This file contains tests for all major functionality of the MPI wrapper library.
 * Run with multiple processes to test communication functions.
 */

#include "mpi_lib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// Test results tracking
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, test_name) do { \
    if (condition) { \
        printf("[PASS] %s\n", test_name); \
        tests_passed++; \
    } else { \
        printf("[FAIL] %s\n", test_name); \
        tests_failed++; \
    } \
} while(0)

void test_initialization(mpi_context_t *ctx) {
    printf("\n=== Testing Initialization ===\n");
    
    // Test context validity
    TEST_ASSERT(ctx != NULL, "Context pointer is valid");
    TEST_ASSERT(ctx->rank >= 0, "Rank is non-negative");
    TEST_ASSERT(ctx->size > 0, "Size is positive");
    TEST_ASSERT(ctx->comm != MPI_COMM_NULL, "Communicator is valid");
    
    // Test utility functions
    TEST_ASSERT(mpi_get_rank(ctx) == ctx->rank, "mpi_get_rank returns correct rank");
    TEST_ASSERT(mpi_get_size(ctx) == ctx->size, "mpi_get_size returns correct size");
    
    // Test root detection
    int expected_root = (ctx->rank == 0) ? 1 : 0;
    TEST_ASSERT(mpi_is_root(ctx) == expected_root, "mpi_is_root works correctly");
    
    // Test version function
    const char *version = mpi_get_version();
    TEST_ASSERT(version != NULL, "Version string is not NULL");
    TEST_ASSERT(strlen(version) > 0, "Version string is not empty");
}

void test_point_to_point(mpi_context_t *ctx) {
    printf("\n=== Testing Point-to-Point Communication ===\n");
    
    if (ctx->size < 2) {
        printf("Skipping point-to-point tests (need at least 2 processes)\n");
        return;
    }
    
    int test_data = 42;
    int received_data = 0;
    
    if (ctx->rank == 0) {
        // Send data to rank 1
        int err = mpi_send(&test_data, 1, MPI_INT, 1, 0, ctx->comm);
        TEST_ASSERT(err == MPI_SUCCESS, "mpi_send successful");
        
        // Receive confirmation back
        err = mpi_recv(&received_data, 1, MPI_INT, 1, 1, ctx->comm, MPI_STATUS_IGNORE);
        TEST_ASSERT(err == MPI_SUCCESS, "mpi_recv successful");
        TEST_ASSERT(received_data == test_data + 1, "Received correct modified data");
        
    } else if (ctx->rank == 1) {
        // Receive data from rank 0
        int err = mpi_recv(&received_data, 1, MPI_INT, 0, 0, ctx->comm, MPI_STATUS_IGNORE);
        TEST_ASSERT(err == MPI_SUCCESS, "mpi_recv successful");
        TEST_ASSERT(received_data == test_data, "Received correct data");
        
        // Send modified data back
        received_data++;
        err = mpi_send(&received_data, 1, MPI_INT, 0, 1, ctx->comm);
        TEST_ASSERT(err == MPI_SUCCESS, "mpi_send successful");
    }
}

void test_collective_operations(mpi_context_t *ctx) {
    printf("\n=== Testing Collective Operations ===\n");
    
    // Test barrier
    int err = mpi_barrier(ctx->comm);
    TEST_ASSERT(err == MPI_SUCCESS, "mpi_barrier successful");
    
    // Test broadcast
    int bcast_data = 0;
    if (ctx->rank == 0) {
        bcast_data = 123;
    }
    
    err = mpi_bcast(&bcast_data, 1, MPI_INT, 0, ctx->comm);
    TEST_ASSERT(err == MPI_SUCCESS, "mpi_bcast successful");
    TEST_ASSERT(bcast_data == 123, "Broadcast data received correctly");
    
    // Test allreduce
    int local_value = ctx->rank + 1;  // 1, 2, 3, ...
    int sum_result = 0;
    
    err = mpi_allreduce(&local_value, &sum_result, 1, MPI_INT, MPI_SUM, ctx->comm);
    TEST_ASSERT(err == MPI_SUCCESS, "mpi_allreduce successful");
    
    // Expected sum: 1 + 2 + ... + size = size * (size + 1) / 2
    int expected_sum = ctx->size * (ctx->size + 1) / 2;
    TEST_ASSERT(sum_result == expected_sum, "Allreduce sum is correct");
}

void test_memory_management(mpi_context_t *ctx) {
    printf("\n=== Testing Memory Management ===\n");
    
    // Test malloc on current rank
    void *ptr = mpi_malloc(1024, ctx->rank, ctx->rank);
    TEST_ASSERT(ptr != NULL, "mpi_malloc successful on current rank");
    
    // Test malloc on different rank (should return NULL)
    void *ptr2 = mpi_malloc(1024, (ctx->rank + 1) % ctx->size, ctx->rank);
    TEST_ASSERT(ptr2 == NULL, "mpi_malloc returns NULL for different rank");
    
    // Test calloc
    void *ptr3 = mpi_calloc(1024, ctx->rank, ctx->rank);
    TEST_ASSERT(ptr3 != NULL, "mpi_calloc successful on current rank");
    
    // Verify calloc initialized to zero
    char *char_ptr = (char*)ptr3;
    int all_zero = 1;
    for (int i = 0; i < 100; i++) {
        if (char_ptr[i] != 0) {
            all_zero = 0;
            break;
        }
    }
    TEST_ASSERT(all_zero, "mpi_calloc initialized memory to zero");
    
    // Test free
    mpi_free(ptr, ctx->rank, ctx->rank);
    mpi_free(ptr3, ctx->rank, ctx->rank);
    printf("[INFO] Memory freed successfully\n");
}

void test_window_management(mpi_context_t *ctx) {
    printf("\n=== Testing Window Management ===\n");
    
    // Allocate memory for window
    int *win_mem = (int*)mpi_malloc(sizeof(int) * 10, ctx->rank, ctx->rank);
    if (!win_mem) {
        printf("Skipping window tests (memory allocation failed)\n");
        return;
    }
    
    // Initialize memory
    for (int i = 0; i < 10; i++) {
        win_mem[i] = ctx->rank * 10 + i;
    }
    
    // Create window
    mpi_window_t win;
    int err = mpi_win_create(win_mem, sizeof(int) * 10, sizeof(int), ctx->comm, &win);
    TEST_ASSERT(err == MPI_SUCCESS, "mpi_win_create successful");
    TEST_ASSERT(win.is_valid == 1, "Window is marked as valid");
    TEST_ASSERT(win.base_ptr == win_mem, "Window base pointer is correct");
    TEST_ASSERT(win.size == sizeof(int) * 10, "Window size is correct");
    TEST_ASSERT(win.element_size == sizeof(int), "Window element size is correct");
    
    // Test window locking
    err = mpi_win_lock_all(&win);
    TEST_ASSERT(err == MPI_SUCCESS, "mpi_win_lock_all successful");
    
    err = mpi_win_unlock_all(&win);
    TEST_ASSERT(err == MPI_SUCCESS, "mpi_win_unlock_all successful");
    
    // Clean up
    mpi_win_destroy(&win);
    TEST_ASSERT(win.is_valid == 0, "Window marked as invalid after destroy");
    
    mpi_free(win_mem, ctx->rank, ctx->rank);
}

void test_one_sided_communication(mpi_context_t *ctx) {
    printf("\n=== Testing One-Sided Communication ===\n");
    
    if (ctx->size < 2) {
        printf("Skipping one-sided tests (need at least 2 processes)\n");
        return;
    }
    
    // Allocate and initialize window memory
    int *win_mem = (int*)mpi_calloc(sizeof(int) * 10, ctx->rank, ctx->rank);
    if (!win_mem) {
        printf("Skipping one-sided tests (memory allocation failed)\n");
        return;
    }
    
    for (int i = 0; i < 10; i++) {
        win_mem[i] = ctx->rank * 100 + i;
    }
    
    // Create window
    mpi_window_t win;
    int err = mpi_win_create(win_mem, sizeof(int) * 10, sizeof(int), ctx->comm, &win);
    if (err != MPI_SUCCESS) {
        printf("Skipping one-sided tests (window creation failed)\n");
        mpi_free(win_mem, ctx->rank, ctx->rank);
        return;
    }
    
    // Lock window
    err = mpi_win_lock_all(&win);
    TEST_ASSERT(err == MPI_SUCCESS, "Window lock successful");
    
    if (ctx->rank == 0) {
        // Put data to rank 1
        int put_data = 999;
        err = mpi_put(&put_data, 1, MPI_INT, 1, 0, &win);
        TEST_ASSERT(err == MPI_SUCCESS, "mpi_put successful");
        
        // Get data from rank 1
        int get_data = 0;
        err = mpi_get(&get_data, 1, MPI_INT, 1, sizeof(int), &win);
        TEST_ASSERT(err == MPI_SUCCESS, "mpi_get successful");
        printf("[INFO] Got data: %d from rank 1\n", get_data);
    }
    
    // Unlock and cleanup
    err = mpi_win_unlock_all(&win);
    TEST_ASSERT(err == MPI_SUCCESS, "Window unlock successful");
    
    mpi_win_destroy(&win);
    mpi_free(win_mem, ctx->rank, ctx->rank);
}

void test_error_handling(mpi_context_t *ctx) {
    printf("\n=== Testing Error Handling ===\n");
    
    // Test NULL pointer handling
    int rank = mpi_get_rank(NULL);
    TEST_ASSERT(rank == -1, "mpi_get_rank handles NULL context");
    
    int size = mpi_get_size(NULL);
    TEST_ASSERT(size == -1, "mpi_get_size handles NULL context");
    
    int is_root = mpi_is_root(NULL);
    TEST_ASSERT(is_root == 0, "mpi_is_root handles NULL context");
    
    // Test invalid window operations
    mpi_window_t invalid_win = {0};
    int err = mpi_win_lock_all(&invalid_win);
    TEST_ASSERT(err != MPI_SUCCESS, "mpi_win_lock_all rejects invalid window");
    
    // Test memory allocation edge cases
    void *ptr = mpi_malloc(0, ctx->rank, ctx->rank);
    TEST_ASSERT(ptr == NULL, "mpi_malloc returns NULL for zero size");
    
    ptr = mpi_calloc(0, ctx->rank, ctx->rank);
    TEST_ASSERT(ptr == NULL, "mpi_calloc returns NULL for zero size");
}

void test_convenience_macros(mpi_context_t *ctx) {
    printf("\n=== Testing Convenience Macros ===\n");
    
    // Test rank-specific execution
    int executed = 0;
    MPI_ON_RANK(ctx, ctx->rank, {
        executed = 1;
    });
    TEST_ASSERT(executed == 1, "MPI_ON_RANK executed on correct rank");
    
    // Test root-specific execution
    executed = 0;
    MPI_ON_ROOT(ctx, {
        executed = 1;
    });
    int expected = (ctx->rank == 0) ? 1 : 0;
    TEST_ASSERT(executed == expected, "MPI_ON_ROOT executed only on root");
    
    // Test constants
    TEST_ASSERT(MPI_LIB_INT == MPI_INT, "MPI_LIB_INT constant is correct");
    TEST_ASSERT(MPI_LIB_SUCCESS == MPI_SUCCESS, "MPI_LIB_SUCCESS constant is correct");
}

void print_test_summary(mpi_context_t *ctx) {
    // Collect test results from all processes
    int global_passed = 0, global_failed = 0;
    
    mpi_allreduce(&tests_passed, &global_passed, 1, MPI_INT, MPI_SUM, ctx->comm);
    mpi_allreduce(&tests_failed, &global_failed, 1, MPI_INT, MPI_SUM, ctx->comm);
    
    if (mpi_is_root(ctx)) {
        printf("\n" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "\n");
        printf("TEST SUMMARY\n");
        printf("=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "\n");
        printf("Total tests passed: %d\n", global_passed);
        printf("Total tests failed: %d\n", global_failed);
        printf("Success rate: %.1f%%\n", 
               (global_passed + global_failed > 0) ? 
               (100.0 * global_passed / (global_passed + global_failed)) : 0.0);
        
        if (global_failed == 0) {
            printf("🎉 ALL TESTS PASSED! 🎉\n");
        } else {
            printf("❌ Some tests failed. Please review the output above.\n");
        }
        printf("=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "\n");
    }
}

int main(int argc, char *argv[]) {
    mpi_context_t ctx;
    
    // Initialize MPI
    int err = mpi_init(argc, argv, &ctx);
    if (err != MPI_SUCCESS) {
        fprintf(stderr, "Failed to initialize MPI\n");
        return 1;
    }
    
    // Print test header
    if (mpi_is_root(&ctx)) {
        printf("MPI Wrapper Library Test Suite\n");
        printf("Library version: %s\n", mpi_get_version());
        printf("Running on %d processes\n\n", ctx.size);
    }
    
    mpi_barrier(ctx.comm);
    
    // Run all tests
    test_initialization(&ctx);
    mpi_barrier(ctx.comm);
    
    test_point_to_point(&ctx);
    mpi_barrier(ctx.comm);
    
    test_collective_operations(&ctx);
    mpi_barrier(ctx.comm);
    
    test_memory_management(&ctx);
    mpi_barrier(ctx.comm);
    
    test_window_management(&ctx);
    mpi_barrier(ctx.comm);
    
    test_one_sided_communication(&ctx);
    mpi_barrier(ctx.comm);
    
    test_error_handling(&ctx);
    mpi_barrier(ctx.comm);
    
    test_convenience_macros(&ctx);
    mpi_barrier(ctx.comm);
    
    // Print summary
    print_test_summary(&ctx);
    
    // Cleanup
    mpi_finalize();
    
    return (tests_failed > 0) ? 1 : 0;
}
