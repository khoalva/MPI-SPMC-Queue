#define _GNU_SOURCE
#include "spmc_queue.h"
#include "../benchmark.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mpi.h>

/**
 * SPMC Queue Benchmark Example
 * 
 * This example demonstrates how to use the benchmark library to measure
 * the performance of the SPMC queue implementation across different scenarios.
 */

// Function prototypes
static int run_benchmark_test(spmc_queue_t *queue, const char *test_type);
static void print_usage(const char *program_name);
static benchmark_config_t get_test_config(const char *test_type);

int main(int argc, char *argv[]) {
    spmc_queue_t queue;
    const char *test_type = "quick";
    
    // Parse command line arguments
    if (argc > 1) {
        test_type = argv[1];
    }
    
    if (strcmp(test_type, "help") == 0 || strcmp(test_type, "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }
    
    // Initialize SPMC queue
    if (spmc_queue_init(&queue, argc, argv) != 0) {
        fprintf(stderr, "Failed to initialize SPMC queue\\n");
        return 1;
    }
    
    // Print initial information - header and common info only from enqueuer
    if (spmc_queue_is_enqueuer(&queue)) {
        printf("\\n=== SPMC Queue Benchmark Suite ===\\n");
        printf("Test type: %s\\n", test_type);
        printf("\\n");
    }
    mpi_print_info(&queue.mpi_ctx);
    
    // Run the benchmark
    int result = run_benchmark_test(&queue, test_type);
    
    // Cleanup
    spmc_queue_destroy(&queue);
    
    return result;
}

static int run_benchmark_test(spmc_queue_t *queue, const char *test_type) {
    benchmark_ctx_t bench_ctx;
    benchmark_config_t config = get_test_config(test_type);
    
    // Initialize benchmark context
    int is_producer = spmc_queue_is_enqueuer(queue);
    int mpi_rank = mpi_get_rank(&queue->mpi_ctx);
    int mpi_size = mpi_get_size(&queue->mpi_ctx);
    
    if (benchmark_init(&bench_ctx, queue, &config, is_producer, mpi_rank, mpi_size) != 0) {
        fprintf(stderr, "Failed to initialize benchmark context\\n");
        return 1;
    }
    
    // Start benchmark timing
    benchmark_start(&bench_ctx);
    
    if (is_producer) {
        // Producer workload
        printf("Rank %d: Starting PRODUCER benchmark\\n", mpi_rank);
        
        // Generate test values
        int *values = malloc(config.num_items * sizeof(int));
        if (!values) {
            fprintf(stderr, "Failed to allocate memory for test values\\n");
            benchmark_cleanup(&bench_ctx);
            return 1;
        }
        
        for (int i = 0; i < config.num_items; i++) {
            values[i] = 100 + (i % 900); // Values between 100-999
        }
        
        // Warmup phase
        if (config.warmup_items > 0) {
            printf("Rank %d: Warmup phase - enqueuing %d items\\n", mpi_rank, config.warmup_items);
            for (int i = 0; i < config.warmup_items && i < config.num_items; i++) {
                spmc_queue_enqueue(queue, values[i]);
                if (config.producer_delay_us > 0) {
                    usleep(config.producer_delay_us);
                }
            }
        }
        
        // Benchmark phase
        printf("Rank %d: Benchmark phase - enqueuing %d items\\n", mpi_rank, config.num_items);
        for (int i = 0; i < config.num_items; i++) {
            BENCHMARK_RECORD_ENQUEUE(&bench_ctx, spmc_queue_enqueue(queue, values[i]));
            
            if (config.producer_delay_us > 0) {
                usleep(config.producer_delay_us);
            }
        }
        
        printf("Rank %d: Producer completed %d enqueue operations\\n", mpi_rank, config.num_items);
        free(values);
        
    } else {
        // Consumer workload
        printf("Rank %d: Starting CONSUMER benchmark\\n", mpi_rank);
        
        int items_consumed = 0;
        int empty_attempts = 0;
        int max_empty_attempts = 100; // Stop after 100 consecutive empty attempts
        
        struct timeval start_time, current_time;
        gettimeofday(&start_time, NULL);
        
        while (items_consumed < config.num_items && empty_attempts < max_empty_attempts) {
            int value;
            BENCHMARK_RECORD_DEQUEUE(&bench_ctx, value = spmc_queue_dequeue(queue));
            
            if (value != -1) {
                items_consumed++;
                empty_attempts = 0;
            } else {
                empty_attempts++;
                
                // Check if we've exceeded the test duration
                gettimeofday(&current_time, NULL);
                double elapsed = (current_time.tv_sec - start_time.tv_sec) + 
                               (current_time.tv_usec - start_time.tv_usec) / 1000000.0;
                
                if (config.test_duration_sec > 0 && elapsed > config.test_duration_sec) {
                    printf("Rank %d: Test duration exceeded, stopping consumer\\n", mpi_rank);
                    break;
                }
            }
            
            if (config.consumer_delay_us > 0) {
                usleep(config.consumer_delay_us);
            }
        }
        
        printf("Rank %d: Consumer completed - consumed %d items\\n", mpi_rank, items_consumed);
    }
    
    // Gán số liệu space complexity cho benchmark_ctx trước khi stop
    bench_ctx.results.queue_capacity_bytes = 0;
    // Luôn gọi hàm getter, không cần macro
    bench_ctx.results.queue_capacity_bytes = spmc_queue_get_capacity_bytes(queue);
    // Stop benchmark timing
    benchmark_stop(&bench_ctx);
    
    // Synchronize all processes before aggregating results
    MPI_Barrier(queue->mpi_ctx.comm);
    
    // Aggregate results from all processes
    if (benchmark_aggregate_results(&bench_ctx, &queue->mpi_ctx.comm) != 0) {
        fprintf(stderr, "Failed to aggregate benchmark results\\n");
        benchmark_cleanup(&bench_ctx);
        return 1;
    }
    
    // Print results (only on rank 0)
    benchmark_print_report(&bench_ctx);
    
    // Export results to CSV
    if (mpi_rank == 0) {
        char filename[256];
        
        // Export standard CSV only
        snprintf(filename, sizeof(filename), "benchmark_%s_%dprocs.csv", test_type, mpi_size);
        if (benchmark_export_csv(&bench_ctx, filename) == 0) {
            printf("CSV exported to: %s\n", filename);
        }
    }
    
    // Cleanup
    benchmark_cleanup(&bench_ctx);
    
    return 0;
}

static benchmark_config_t get_test_config(const char *test_type) {
    if (strcmp(test_type, "quick") == 0) {
        return benchmark_config_quick_test(1000);
    } else if (strcmp(test_type, "throughput") == 0) {
        return benchmark_config_throughput_test(10000);
    } else if (strcmp(test_type, "latency") == 0) {
        return benchmark_config_latency_test(5000);
    } else if (strcmp(test_type, "scalability") == 0) {
        return benchmark_config_scalability_test(20000, 8);
    } else if (strcmp(test_type, "stress") == 0) {
        return benchmark_config_stress_test(60);
    } else {
        printf("Unknown test type '%s', using quick test\\n", test_type);
        return benchmark_config_throughput_test(1000);
    }
}

static void print_usage(const char *program_name) {
    printf("\\nSPMC Queue Benchmark Tool\\n");
    printf("=========================\\n\\n");
    printf("Usage: mpirun -np <processes> %s [test_type]\\n\\n", program_name);
    printf("Available test types:\\n");
    printf("  quick       - Quick test with 1000 items (default)\\n");
    printf("  throughput  - Throughput test with 10000 items\\n");
    printf("  latency     - Latency analysis with detailed timing\\n");
    printf("  scalability - Scalability test with varying process counts\\n");
    printf("  stress      - Stress test with high load for 60 seconds\\n");
    printf("  help        - Show this help message\\n\\n");
    printf("Examples:\\n");
    printf("  mpirun -np 3 %s quick\\n", program_name);
    printf("  mpirun -np 4 %s throughput\\n", program_name);
    printf("  mpirun -np 6 %s scalability\\n", program_name);
    printf("\\n");
}
