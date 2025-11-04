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
    
    // Special handling for enqueue_only and dequeue_only tests
    int is_enqueue_only = (strcmp(test_type, "enqueue_only") == 0);
    int is_dequeue_only = (strcmp(test_type, "dequeue_only") == 0);
    
    if (benchmark_init(&bench_ctx, queue, &config, is_producer, mpi_rank, mpi_size) != 0) {
        fprintf(stderr, "Failed to initialize benchmark context\\n");
        return 1;
    }
    
    // For dequeue_only test, prefill the queue first (only by rank 0)
    if (is_dequeue_only && mpi_rank == 0) {
        int prefill_items = config.warmup_items; // warmup_items stores prefill count
        printf("Rank %d: Prefilling queue with %d items for dequeue_only test\n", mpi_rank, prefill_items);
        
        int *prefill_values = malloc(prefill_items * sizeof(int));
        if (!prefill_values) {
            fprintf(stderr, "Failed to allocate memory for prefill values\n");
            benchmark_cleanup(&bench_ctx);
            return 1;
        }
        
        for (int i = 0; i < prefill_items; i++) {
            prefill_values[i] = 100 + (i % 900);
            spmc_queue_enqueue(queue, prefill_values[i]);
        }
        
        free(prefill_values);
        printf("Rank %d: Queue prefilled with %d items\n", mpi_rank, prefill_items);
    }
    
    // Synchronize after prefill - critical for dequeue_only test
    MPI_Barrier(queue->mpi_ctx.comm);
    
    // Important: Add a small delay after barrier to ensure all processes are truly ready
    if (is_dequeue_only) {
        usleep(10000); // 10ms delay to ensure prefill is fully visible to all processes
    }
    
    // Start benchmark timing AFTER synchronization
    benchmark_start(&bench_ctx);
    
    if (is_producer && !is_dequeue_only) {
        // Producer workload (skip for dequeue_only test)
        printf("Rank %d: Starting PRODUCER benchmark\\n", mpi_rank);
        
        // Generate test values - allocate for both warmup and benchmark items
        int total_items = config.warmup_items + config.num_items;
        
        // For enqueue_only test, no warmup needed
        if (is_enqueue_only) {
            total_items = config.num_items;
        }
        
        int *values = malloc(total_items * sizeof(int));
        if (!values) {
            fprintf(stderr, "Failed to allocate memory for test values\n");
            benchmark_cleanup(&bench_ctx);
            return 1;
        }
        
        for (int i = 0; i < total_items; i++) {
            values[i] = 100 + (i % 900); // Values between 100-999
        }
        
        // Warmup phase (skip for enqueue_only)
        if (config.warmup_items > 0 && !is_enqueue_only) {
            printf("Rank %d: Warmup phase - enqueuing %d items\n", mpi_rank, config.warmup_items);
            for (int i = 0; i < config.warmup_items; i++) {
                spmc_queue_enqueue(queue, values[i]);
                if (config.producer_delay_us > 0) {
                    usleep(config.producer_delay_us);
                }
            }
        }
        
        // Benchmark phase
        printf("Rank %d: Benchmark phase - enqueuing %d items\n", mpi_rank, config.num_items);
        int start_index = is_enqueue_only ? 0 : config.warmup_items;
        for (int i = 0; i < config.num_items; i++) {
            int value_index = start_index + i;
            if (value_index < total_items) {
                BENCHMARK_RECORD_ENQUEUE(&bench_ctx, spmc_queue_enqueue(queue, values[value_index]));
            }
            
            if (config.producer_delay_us > 0) {
                usleep(config.producer_delay_us);
            }
        }
        
        printf("Rank %d: Producer completed %d enqueue operations\n", mpi_rank, config.num_items);
        
        // Record producer finish time for accurate throughput calculation
        benchmark_record_producer_finish(&bench_ctx);
        printf("Rank %d: Producer finish time recorded\n", mpi_rank);
        
        free(values);
        
    } else if (!is_producer || is_dequeue_only) {
        // Consumer workload (or dequeue_only for all ranks except prefiller)
        if (is_dequeue_only && mpi_rank == 0) {
            // Rank 0 was the prefiller, it doesn't consume in dequeue_only test
            printf("Rank %d: Prefiller process, not consuming\n", mpi_rank);
        } else {
            printf("Rank %d: Starting CONSUMER benchmark\n", mpi_rank);
            
            int items_consumed = 0;
            int empty_attempts = 0;
            int max_empty_attempts = 100; // Stop after 100 consecutive empty attempts
            
            // For dequeue_only, we divide total items among consumers (excluding rank 0)
            int total_items_to_consume;
            if (is_dequeue_only) {
                // Number of consumers is mpi_size - 1 (excluding rank 0 prefiller)
                int num_consumers = mpi_size - 1;
                // Each consumer tries to dequeue approximately equal share
                // But we allow them to consume as much as available
                total_items_to_consume = config.num_items; // Try to consume all prefilled items
                printf("Rank %d: Target items to consume: %d (total prefilled: %d, consumers: %d)\n", 
                       mpi_rank, total_items_to_consume, config.num_items, num_consumers);
            } else {
                total_items_to_consume = config.warmup_items + config.num_items;
            }
            
            struct timeval start_time, current_time;
            gettimeofday(&start_time, NULL);
            
            // Get batch size from queue implementation
            int batch_size = spmc_queue_get_batch_size(queue);
            int *buffer = malloc(batch_size * sizeof(int));
            if (!buffer) {
                fprintf(stderr, "Rank %d: Failed to allocate dequeue buffer\n", mpi_rank);
                benchmark_cleanup(&bench_ctx);
                return 1;
            }
            
            // For dequeue_only, don't limit by total_items_to_consume for individual consumers
            // Just keep dequeuing until queue is empty or timeout
            while (empty_attempts < max_empty_attempts) {
                int count;
                BENCHMARK_RECORD_DEQUEUE(&bench_ctx, count = spmc_queue_dequeue(queue, buffer, batch_size));
                
                if (count > 0) {
                    // Successfully dequeued 'count' items
                    items_consumed += count;
                    empty_attempts = 0;
                    
                    // For dequeue_only, continue until queue is empty
                    if (!is_dequeue_only && items_consumed >= total_items_to_consume) {
                        printf("Rank %d: Reached target items, stopping\n", mpi_rank);
                        break;
                    }
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
            
            free(buffer);
            
            printf("Rank %d: Consumer completed - consumed %d items\\n", mpi_rank, items_consumed);
            
            // Record consumer finish time for accurate throughput calculation
            benchmark_record_consumer_finish(&bench_ctx);
            printf("Rank %d: Consumer finish time recorded\n", mpi_rank);
        }
    }
    
    // CRITICAL: Synchronize ALL processes before stopping benchmark
    // This ensures all consumers have finished before we calculate results
    MPI_Barrier(queue->mpi_ctx.comm);
    
    // Gán số liệu space complexity cho benchmark_ctx trước khi stop
    bench_ctx.results.queue_capacity_bytes = 0;
    // Luôn gọi hàm getter, không cần macro
    bench_ctx.results.queue_capacity_bytes = spmc_queue_get_capacity_bytes(queue);
    
    // Stop benchmark timing - now all processes are done
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
    } else if (strcmp(test_type, "enqueue_only") == 0) {
        return benchmark_config_enqueue_only_test(100000);
    } else if (strcmp(test_type, "dequeue_only") == 0) {
        return benchmark_config_dequeue_only_test(100000, 100000);
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
    printf("  quick        - Quick test with 1000 items (default)\\n");
    printf("  throughput   - Throughput test with 10000 items\\n");
    printf("  latency      - Latency analysis with detailed timing\\n");
    printf("  scalability  - Scalability test with varying process counts\\n");
    printf("  stress       - Stress test with high load for 60 seconds\\n");
    printf("  enqueue_only - Enqueue-only throughput test (100K items, 1 process)\\n");
    printf("  dequeue_only - Dequeue-only throughput test (prefill 100K, dequeue 100K)\\n");
    printf("  help         - Show this help message\\n\\n");
    printf("Examples:\\n");
    printf("  mpirun -np 3 %s quick\\n", program_name);
    printf("  mpirun -np 4 %s throughput\\n", program_name);
    printf("  mpirun -np 6 %s scalability\\n", program_name);
    printf("  mpirun -np 1 %s enqueue_only\\n", program_name);
    printf("  mpirun -np 5 %s dequeue_only\\n", program_name);
    printf("\\n");
}
