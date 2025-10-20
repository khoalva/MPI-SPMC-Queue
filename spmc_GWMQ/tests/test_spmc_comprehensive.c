#define _POSIX_C_SOURCE 199309L
#include "../src/spmc_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
/**
 * Comprehensive Test Suite Runner
 * Runs multiple test scenarios in sequence to verify different aspects
 */

typedef struct {
    const char* name;
    const char* description;
    int min_processes;
    void (*test_function)(spmc_queue_t*);
} test_case_t;

// Forward declarations of test functions
void run_basic_test(spmc_queue_t *queue);
void run_burst_test(spmc_queue_t *queue);
void run_fairness_test(spmc_queue_t *queue);
void run_resilience_test(spmc_queue_t *queue);

void run_basic_test(spmc_queue_t *queue) {
    int rank = mpi_get_rank(&queue->mpi_ctx);
    
    if (spmc_queue_is_enqueuer(queue)) {
        printf("Basic Test: Producer enqueuing 20 items\n");
        for (int i = 1; i <= 20; i++) {
            spmc_queue_enqueue(queue, i * 5);
            if (i % 5 == 0) usleep(10000);
        }
    } else {
        printf("Basic Test: Consumer %d dequeuing items\n", rank - 1);
        int count = 0;
        for (int i = 0; i < 50 && count < 15; i++) {
            int val = spmc_queue_dequeue(queue);
            if (val != -1) {
                count++;
                printf("Consumer %d got: %d\n", rank - 1, val);
            }
            usleep(8000);
        }
    }
}

void run_burst_test(spmc_queue_t *queue) {
    int rank = mpi_get_rank(&queue->mpi_ctx);
    
    if (spmc_queue_is_enqueuer(queue)) {
        printf("Burst Test: Rapid enqueuing\n");
        for (int i = 100; i < 150; i++) {
            spmc_queue_enqueue(queue, i);
        }
    } else {
        printf("Burst Test: Consumer %d rapid dequeuing\n", rank - 1);
        int count = 0;
        for (int i = 0; i < 80; i++) {
            int val = spmc_queue_dequeue(queue);
            if (val != -1) count++;
            usleep(2000);
        }
        printf("Consumer %d burst result: %d items\n", rank - 1, count);
    }
}

void run_fairness_test(spmc_queue_t *queue) {
    int rank = mpi_get_rank(&queue->mpi_ctx);
    
    if (spmc_queue_is_enqueuer(queue)) {
        printf("Fairness Test: Controlled production\n");
        for (int i = 200; i < 250; i++) {
            spmc_queue_enqueue(queue, i);
            usleep(5000);  // Controlled pace
        }
    } else {
        printf("Fairness Test: Consumer %d with equal opportunity\n", rank - 1);
        int count = 0;
        for (int i = 0; i < 100; i++) {
            int val = spmc_queue_dequeue(queue);
            if (val != -1) count++;
            usleep(7000);  // Equal timing for all consumers
        }
        printf("Consumer %d fairness result: %d items\n", rank - 1, count);
    }
}

void run_resilience_test(spmc_queue_t *queue) {
    int rank = mpi_get_rank(&queue->mpi_ctx);
    
    if (spmc_queue_is_enqueuer(queue)) {
        printf("Resilience Test: Variable rate production\n");
        for (int i = 300; i < 330; i++) {
            spmc_queue_enqueue(queue, i);
            // Variable delays to test consumer adaptation
            usleep((i % 3) * 15000);
        }
    } else {
        printf("Resilience Test: Consumer %d adaptive behavior\n", rank - 1);
        int count = 0;
        int consecutive_empty = 0;
        
        for (int i = 0; i < 80; i++) {
            int val = spmc_queue_dequeue(queue);
            if (val != -1) {
                count++;
                consecutive_empty = 0;
            } else {
                consecutive_empty++;
            }
            
            // Adaptive delay based on success rate
            if (consecutive_empty > 5) {
                usleep(20000);  // Longer delay after many failures
            } else {
                usleep(3000);   // Short delay when successful
            }
        }
        printf("Consumer %d resilience result: %d items\n", rank - 1, count);
    }
}

int main(int argc, char *argv[]) {
    spmc_queue_t queue;
    
    // Initialize queue
    if (spmc_queue_init(&queue, argc, argv) != MPI_SUCCESS) {
        fprintf(stderr, "Failed to initialize SPMC queue\n");
        return -1;
    }
    
    int rank = mpi_get_rank(&queue.mpi_ctx);
    int size = mpi_get_size(&queue.mpi_ctx);
    
    printf("Starting comprehensive test suite on rank %d/%d\n", rank, size);
    
    if (size < 2) {
        fprintf(stderr, "This test requires at least 2 processes\n");
        spmc_queue_destroy(&queue);
        return -1;
    }
    
    // Define test cases
    test_case_t tests[] = {
        {"Basic Functionality", "Basic enqueue/dequeue operations", 2, run_basic_test},
        {"Burst Performance", "High-speed burst operations", 2, run_burst_test},
        {"Consumer Fairness", "Fair distribution among consumers", 3, run_fairness_test},
        {"System Resilience", "Adaptation to varying conditions", 2, run_resilience_test}
    };
    
    int num_tests = sizeof(tests) / sizeof(tests[0]);
    
    for (int t = 0; t < num_tests; t++) {
        if (size >= tests[t].min_processes) {
            if (rank == 0) {
                printf("\n" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "\n");
                printf("Test %d/%d: %s\n", t + 1, num_tests, tests[t].name);
                printf("Description: %s\n", tests[t].description);
                printf("=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "\n");
            }
            
            MPI_TRY(mpi_barrier(queue.mpi_ctx.comm));
            
            // Run the test
            tests[t].test_function(&queue);
            
            MPI_TRY(mpi_barrier(queue.mpi_ctx.comm));
            
            if (rank == 0) {
                printf("Test %d completed.\n", t + 1);
            }
            
            // Delay between tests
            usleep(100000);  // 100ms
            
        } else {
            if (rank == 0) {
                printf("Skipping test '%s' - requires %d processes, have %d\n", 
                       tests[t].name, tests[t].min_processes, size);
            }
        }
    }
    
    if (rank == 0) {
        printf("\n" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "\n");
        printf("Comprehensive Test Suite Completed\n");
        printf("=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "\n");
        spmc_queue_print_stats(&queue);
    }
    
    // Cleanup
    spmc_queue_destroy(&queue);
    
    return 0;
}