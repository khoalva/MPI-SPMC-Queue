#include "micro_benchmark.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

/**
 * Micro Benchmark Library Implementation
 * 
 * Implements proper micro benchmarking methodology for SPMC queues
 */

// Internal helper functions
static void print_separator(void);
static double calculate_std_dev(double *values, int count, double mean);

/**
 * Initialize micro benchmark context
 */
int micro_bench_init(micro_bench_ctx_t *ctx, 
                     void *queue,
                     const micro_bench_config_t *config,
                     int mpi_rank,
                     int mpi_size,
                     MPI_Comm mpi_comm) {
    if (!ctx || !queue || !config) {
        return -1;
    }
    
    memset(ctx, 0, sizeof(micro_bench_ctx_t));
    
    ctx->queue = queue;
    ctx->config = *config;
    ctx->mpi_rank = mpi_rank;
    ctx->mpi_size = mpi_size;
    ctx->mpi_comm = mpi_comm;
    
    // Initialize local result
    ctx->local_result.rank = mpi_rank;
    ctx->local_result.operations_completed = 0;
    ctx->local_result.execution_time_sec = 0.0;
    ctx->local_result.throughput_ops_per_sec = 0.0;
    
    // Record global benchmark start time (synchronized across all processes)
    MPI_Barrier(mpi_comm);
    gettimeofday(&ctx->benchmark_start, NULL);
    
    return 0;
}

/**
 * Create default micro benchmark configuration
 */
micro_bench_config_t micro_bench_config_create(int num_consumers, int ops_per_consumer) {
    micro_bench_config_t config;
    memset(&config, 0, sizeof(config));
    
    config.num_consumers = num_consumers;
    config.ops_per_consumer = (ops_per_consumer > 0) ? ops_per_consumer : DEFAULT_OPS_PER_CONSUMER;
    config.enable_verbose = 0;
    snprintf(config.test_name, sizeof(config.test_name), 
             "Micro Benchmark (%d consumers x %d ops)", 
             num_consumers, config.ops_per_consumer);
    
    return config;
}

/**
 * Phase 1: Warmup - Producer fills queue with N items (not measured)
 */
int micro_bench_warmup_phase(micro_bench_ctx_t *ctx, 
                              int (*enqueue_func)(void *queue, int value)) {
    if (!ctx || !enqueue_func) {
        return -1;
    }
    
    // Only rank 0 (producer) performs warmup
    if (ctx->mpi_rank != 0) {
        return 0;
    }
    
    int total_warmup_items = ctx->config.num_consumers * ctx->config.ops_per_consumer;
    
    printf("\n=== Micro Benchmark: %s ===\n", ctx->config.test_name);
    print_separator();
    printf("Warmup Phase (not measured):\n");
    printf("  Prefilling queue with %d items (N)\n", total_warmup_items);
    printf("  Formula: %d consumers x %d ops_per_consumer\n", 
           ctx->config.num_consumers, ctx->config.ops_per_consumer);
    print_separator();
    
    // Enqueue warmup items
    for (int i = 0; i < total_warmup_items; i++) {
        int value = 1000 + i; // Distinct values for verification
        int result = enqueue_func(ctx->queue, value);
        
        if (result != 0) {
            fprintf(stderr, "ERROR: Warmup enqueue failed at item %d/%d\n", 
                    i, total_warmup_items);
            return -1;
        }
        
        // Progress indicator every 10%
        if ((i + 1) % (total_warmup_items / 10) == 0) {
            printf("  Progress: %d%%\n", ((i + 1) * 100) / total_warmup_items);
        }
    }
    
    printf("  Warmup completed: %d items enqueued\n", total_warmup_items);
    print_separator();
    
    return total_warmup_items;
}

/**
 * Phase 2: Measurement - Consumer performs fixed operations
 */
int micro_bench_consumer_phase(micro_bench_ctx_t *ctx,
                                int (*dequeue_func)(void *queue, int *value)) {
    if (!ctx || !dequeue_func) {
        return -1;
    }
    
    int target_ops = ctx->config.ops_per_consumer;
    int completed_ops = 0;
    
    // Synchronize all consumers before starting measurement
    MPI_Barrier(ctx->mpi_comm);
    
    printf("Rank %d: Starting consumer phase (%d operations)\n", 
           ctx->mpi_rank, target_ops);
    
    // Start timing
    MICRO_BENCH_START_TIMER(ctx);
    
    // Record start time relative to benchmark start
    struct timeval now;
    gettimeofday(&now, NULL);
    ctx->local_result.start_time_sec = 
        (now.tv_sec - ctx->benchmark_start.tv_sec) + 
        (now.tv_usec - ctx->benchmark_start.tv_usec) / 1000000.0;
    
    // Perform exactly target_ops dequeue operations
    while (completed_ops < target_ops) {
        int value = 0;
        int result = dequeue_func(ctx->queue, &value);
        
        if (result == 1) {
            // Successful dequeue
            completed_ops++;
        } else if (result == 0) {
            // Queue empty - this should NOT happen in micro benchmark
            // But we handle it gracefully
            fprintf(stderr, "WARNING: Rank %d encountered empty queue at op %d/%d\n",
                    ctx->mpi_rank, completed_ops, target_ops);
            usleep(100); // Brief wait before retry
        } else {
            // Error
            fprintf(stderr, "ERROR: Rank %d dequeue failed at op %d/%d\n",
                    ctx->mpi_rank, completed_ops, target_ops);
            return -1;
        }
    }
    
    // Stop timing
    MICRO_BENCH_STOP_TIMER(ctx);
    
    // Record finish time relative to benchmark start
    gettimeofday(&now, NULL);
    ctx->local_result.finish_time_sec = 
        (now.tv_sec - ctx->benchmark_start.tv_sec) + 
        (now.tv_usec - ctx->benchmark_start.tv_usec) / 1000000.0;
    
    // Calculate local results
    ctx->local_result.operations_completed = completed_ops;
    ctx->local_result.execution_time_sec = MICRO_BENCH_CALC_ELAPSED(ctx);
    
    if (ctx->local_result.execution_time_sec > 0.0) {
        ctx->local_result.throughput_ops_per_sec = 
            (double)completed_ops / ctx->local_result.execution_time_sec;
    }
    
    printf("Rank %d: Completed %d operations in %.6f seconds (%.2f ops/sec)\n",
           ctx->mpi_rank, 
           completed_ops,
           ctx->local_result.execution_time_sec,
           ctx->local_result.throughput_ops_per_sec);
    
    return completed_ops;
}

/**
 * Phase 2A: Producer measurement - Enqueue 2N items (measured)
 */
int micro_bench_producer_phase(micro_bench_ctx_t *ctx,
                                int (*enqueue_func)(void *queue, int value)) {
    if (!ctx || !enqueue_func) {
        return -1;
    }
    
    // Only rank 0 (producer) performs enqueue
    if (ctx->mpi_rank != 0) {
        return 0;
    }
    
    // Calculate total items to enqueue: 2x what consumers will dequeue in measurement
    int total_items = ctx->config.num_consumers * ctx->config.ops_per_consumer * 2;
    
    printf("\nRank 0: Starting producer measurement phase\n");
    printf("Rank 0: Will enqueue %d items (2N = 2 x %d consumers x %d ops)\n",
           total_items, ctx->config.num_consumers, ctx->config.ops_per_consumer);
    
    // Synchronize with consumers before starting
    MPI_Barrier(ctx->mpi_comm);
    
    // Start timing
    MICRO_BENCH_START_TIMER(ctx);
    
    // Record start time relative to benchmark start
    struct timeval now;
    gettimeofday(&now, NULL);
    ctx->local_result.start_time_sec = 
        (now.tv_sec - ctx->benchmark_start.tv_sec) + 
        (now.tv_usec - ctx->benchmark_start.tv_usec) / 1000000.0;
    
    // Enqueue exactly total_items
    int base_value = 100000; // Different range from warmup
    int enqueued_count = 0;
    
    for (int i = 0; i < total_items; i++) {
        int value = base_value + i;
        int result = enqueue_func(ctx->queue, value);
        
        if (result == 0) {
            enqueued_count++;
        } else {
            fprintf(stderr, "WARNING: Rank 0 enqueue failed at item %d/%d\n",
                    i, total_items);
        }
        
        // Progress indicator every 10%
        if ((i + 1) % (total_items / 10) == 0 && ctx->config.enable_verbose) {
            printf("Rank 0: Enqueue progress: %d%%\n", 
                   ((i + 1) * 100) / total_items);
        }
    }
    
    // Stop timing
    MICRO_BENCH_STOP_TIMER(ctx);
    
    // Record finish time relative to benchmark start
    gettimeofday(&now, NULL);
    ctx->local_result.finish_time_sec = 
        (now.tv_sec - ctx->benchmark_start.tv_sec) + 
        (now.tv_usec - ctx->benchmark_start.tv_usec) / 1000000.0;
    
    // Calculate local results
    ctx->local_result.operations_completed = enqueued_count;
    ctx->local_result.execution_time_sec = MICRO_BENCH_CALC_ELAPSED(ctx);
    
    if (ctx->local_result.execution_time_sec > 0.0) {
        ctx->local_result.throughput_ops_per_sec = 
            (double)enqueued_count / ctx->local_result.execution_time_sec;
    }
    
    printf("Rank 0: Completed %d enqueue operations in %.6f seconds (%.2f ops/sec)\n",
           enqueued_count,
           ctx->local_result.execution_time_sec,
           ctx->local_result.throughput_ops_per_sec);
    
    return enqueued_count;
}

/**
 * Phase 3: Consumer drain phase - Dequeue remaining items (not measured)
 */
int micro_bench_consumer_drain(micro_bench_ctx_t *ctx,
                                int (*dequeue_func)(void *queue, int *value),
                                int drain_items) {
    if (!ctx || !dequeue_func) {
        return -1;
    }
    
    // Only consumers (non-rank-0) perform drain
    if (ctx->mpi_rank == 0) {
        return 0;
    }
    
    // Calculate items per consumer for drain phase
    int items_per_consumer = drain_items / ctx->config.num_consumers;
    int drained_count = 0;
    
    printf("Rank %d: Starting drain phase (%d items, not measured)\n",
           ctx->mpi_rank, items_per_consumer);
    
    // Dequeue items without timing
    for (int i = 0; i < items_per_consumer; i++) {
        int value = 0;
        int result = dequeue_func(ctx->queue, &value);
        
        if (result == 1) {
            drained_count++;
        } else if (result == 0) {
            // Queue empty - this might happen if producer was slower
            // Just break and continue
            break;
        } else {
            // Error - but don't fail the benchmark
            fprintf(stderr, "WARNING: Rank %d drain dequeue failed at item %d/%d\n",
                    ctx->mpi_rank, i, items_per_consumer);
            break;
        }
    }
    
    printf("Rank %d: Drained %d items\n", ctx->mpi_rank, drained_count);
    
    return drained_count;
}

/**
 * Aggregate results from all consumer processes
 */
int micro_bench_aggregate_results(micro_bench_ctx_t *ctx, int gather_process_details) {
    if (!ctx) {
        return -1;
    }
    
    // Gather all consumer finish times to determine total benchmark duration
    double *all_finish_times = NULL;
    if (ctx->mpi_rank == 0) {
        all_finish_times = malloc(ctx->mpi_size * sizeof(double));
        if (!all_finish_times) {
            return -1;
        }
    }
    
    MPI_Gather(&ctx->local_result.finish_time_sec, 1, MPI_DOUBLE,
               all_finish_times, 1, MPI_DOUBLE,
               0, ctx->mpi_comm);
    
    // Calculate total benchmark duration (max finish time across all consumers)
    if (ctx->mpi_rank == 0) {
        ctx->global_results.total_time_sec = 0.0;
        for (int i = 0; i < ctx->mpi_size; i++) {
            if (all_finish_times[i] > ctx->global_results.total_time_sec) {
                ctx->global_results.total_time_sec = all_finish_times[i];
            }
        }
        free(all_finish_times);
    }
    
    // Gather execution times for statistics
    double *all_exec_times = NULL;
    if (ctx->mpi_rank == 0) {
        all_exec_times = malloc(ctx->mpi_size * sizeof(double));
        if (!all_exec_times) {
            return -1;
        }
    }
    
    MPI_Gather(&ctx->local_result.execution_time_sec, 1, MPI_DOUBLE,
               all_exec_times, 1, MPI_DOUBLE,
               0, ctx->mpi_comm);
    
    // Calculate statistics on rank 0
    if (ctx->mpi_rank == 0) {
        int num_consumers = ctx->config.num_consumers;
        
        // Total operations
        ctx->global_results.total_operations = 
            (long)num_consumers * ctx->config.ops_per_consumer;
        ctx->global_results.num_consumers = num_consumers;
        
        // Calculate total throughput
        if (ctx->global_results.total_time_sec > 0.0) {
            ctx->global_results.total_throughput_ops_per_sec = 
                (double)ctx->global_results.total_operations / 
                ctx->global_results.total_time_sec;
        }
        
        // Calculate consumer time statistics
        double sum = 0.0;
        ctx->global_results.min_consumer_time_sec = all_exec_times[1]; // Skip rank 0 (producer)
        ctx->global_results.max_consumer_time_sec = all_exec_times[1];
        
        for (int i = 1; i < ctx->mpi_size; i++) { // Skip rank 0
            sum += all_exec_times[i];
            if (all_exec_times[i] < ctx->global_results.min_consumer_time_sec) {
                ctx->global_results.min_consumer_time_sec = all_exec_times[i];
            }
            if (all_exec_times[i] > ctx->global_results.max_consumer_time_sec) {
                ctx->global_results.max_consumer_time_sec = all_exec_times[i];
            }
        }
        
        ctx->global_results.avg_consumer_time_sec = sum / num_consumers;
        
        // Calculate standard deviation
        ctx->global_results.std_dev_consumer_time_sec = 
            calculate_std_dev(all_exec_times + 1, num_consumers, 
                            ctx->global_results.avg_consumer_time_sec);
        
        free(all_exec_times);
    }
    
    // Gather detailed per-process results if requested
    if (gather_process_details) {
        if (ctx->mpi_rank == 0) {
            ctx->global_results.process_results = 
                malloc(ctx->mpi_size * sizeof(micro_process_result_t));
            
            if (!ctx->global_results.process_results) {
                return -1;
            }
        }
        
        // All ranks must participate in MPI_Gather
        MPI_Gather(&ctx->local_result, sizeof(micro_process_result_t), MPI_BYTE,
                   ctx->global_results.process_results, 
                   sizeof(micro_process_result_t), MPI_BYTE,
                   0, ctx->mpi_comm);
    }
    
    return 0;
}

/**
 * Print benchmark results
 */
void micro_bench_print_results(const micro_bench_ctx_t *ctx) {
    if (!ctx || ctx->mpi_rank != 0) {
        return;
    }
    
    const micro_bench_results_t *results = &ctx->global_results;
    
    printf("\n");
    print_separator();
    printf("=== MICRO BENCHMARK RESULTS ===\n");
    print_separator();
    
    printf("\nConfiguration:\n");
    printf("  Test name:            %s\n", ctx->config.test_name);
    printf("  Number of consumers:  %d\n", results->num_consumers);
    printf("  Ops per consumer:     %d\n", ctx->config.ops_per_consumer);
    printf("  Total operations:     %ld\n", results->total_operations);
    
    printf("\nThroughput:\n");
    printf("  Total time:           %.6f seconds\n", results->total_time_sec);
    printf("  Total throughput:     %.2f ops/sec\n", results->total_throughput_ops_per_sec);
    printf("  Total throughput:     %.2f Mops/sec\n", 
           results->total_throughput_ops_per_sec / 1000000.0);
    
    printf("\nConsumer Statistics:\n");
    printf("  Average time:         %.6f seconds\n", results->avg_consumer_time_sec);
    printf("  Min time:             %.6f seconds\n", results->min_consumer_time_sec);
    printf("  Max time:             %.6f seconds\n", results->max_consumer_time_sec);
    printf("  Std deviation:        %.6f seconds\n", results->std_dev_consumer_time_sec);
    printf("  Time variation:       %.2f%%\n", 
           (results->std_dev_consumer_time_sec / results->avg_consumer_time_sec) * 100.0);
    
    // Print per-process details if available
    if (ctx->config.enable_verbose && results->process_results) {
        printf("\nPer-Process Details:\n");
        printf("  %-6s %-8s %-12s %-15s %-15s %-15s %-15s\n",
               "Rank", "Role", "Operations", "Time (sec)", "Throughput", "Start (sec)", "Finish (sec)");
        print_separator();
        
        // Print producer (rank 0)
        if (results->process_results[0].operations_completed > 0) {
            micro_process_result_t *pr = &results->process_results[0];
            printf("  %-6d %-8s %-12ld %-15.6f %-15.2f %-15.6f %-15.6f\n",
                   pr->rank, "Producer",
                   pr->operations_completed,
                   pr->execution_time_sec,
                   pr->throughput_ops_per_sec,
                   pr->start_time_sec,
                   pr->finish_time_sec);
        }
        
        // Print consumers
        for (int i = 1; i < ctx->mpi_size; i++) {
            micro_process_result_t *pr = &results->process_results[i];
            printf("  %-6d %-8s %-12ld %-15.6f %-15.2f %-15.6f %-15.6f\n",
                   pr->rank, "Consumer",
                   pr->operations_completed,
                   pr->execution_time_sec,
                   pr->throughput_ops_per_sec,
                   pr->start_time_sec,
                   pr->finish_time_sec);
        }
    }
    
    print_separator();
    printf("\n");
}

/**
 * Export results to CSV file
 */
int micro_bench_export_csv(const micro_bench_ctx_t *ctx, const char *filename) {
    if (!ctx || !filename || ctx->mpi_rank != 0) {
        return -1;
    }
    
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        fprintf(stderr, "ERROR: Cannot open file %s for writing\n", filename);
        return -1;
    }
    
    const micro_bench_results_t *results = &ctx->global_results;
    
    // Write summary
    fprintf(fp, "# Micro Benchmark Results\n");
    fprintf(fp, "# Test: %s\n", ctx->config.test_name);
    fprintf(fp, "# Consumers: %d, Ops per consumer: %d\n", 
            results->num_consumers, ctx->config.ops_per_consumer);
    fprintf(fp, "\n");
    
    fprintf(fp, "Metric,Value,Unit\n");
    fprintf(fp, "Total Operations,%ld,operations\n", results->total_operations);
    fprintf(fp, "Total Time,%.6f,seconds\n", results->total_time_sec);
    fprintf(fp, "Total Throughput,%.2f,ops/sec\n", results->total_throughput_ops_per_sec);
    fprintf(fp, "Avg Consumer Time,%.6f,seconds\n", results->avg_consumer_time_sec);
    fprintf(fp, "Min Consumer Time,%.6f,seconds\n", results->min_consumer_time_sec);
    fprintf(fp, "Max Consumer Time,%.6f,seconds\n", results->max_consumer_time_sec);
    fprintf(fp, "Std Dev Consumer Time,%.6f,seconds\n", results->std_dev_consumer_time_sec);
    
    // Write per-process details if available
    if (results->process_results) {
        fprintf(fp, "\n# Per-Process Details\n");
        fprintf(fp, "Rank,Role,Operations,Time_sec,Throughput_ops_per_sec,Start_sec,Finish_sec\n");
        
        // Write producer (rank 0)
        if (results->process_results[0].operations_completed > 0) {
            micro_process_result_t *pr = &results->process_results[0];
            fprintf(fp, "%d,Producer,%ld,%.6f,%.2f,%.6f,%.6f\n",
                   pr->rank,
                   pr->operations_completed,
                   pr->execution_time_sec,
                   pr->throughput_ops_per_sec,
                   pr->start_time_sec,
                   pr->finish_time_sec);
        }
        
        // Write consumers
        for (int i = 1; i < ctx->mpi_size; i++) {
            micro_process_result_t *pr = &results->process_results[i];
            fprintf(fp, "%d,Consumer,%ld,%.6f,%.2f,%.6f,%.6f\n",
                   pr->rank,
                   pr->operations_completed,
                   pr->execution_time_sec,
                   pr->throughput_ops_per_sec,
                   pr->start_time_sec,
                   pr->finish_time_sec);
        }
    }
    
    fclose(fp);
    printf("Results exported to: %s\n", filename);
    
    return 0;
}

int micro_bench_export_csv_with_memory(const micro_bench_ctx_t *ctx, const char *filename, size_t queue_memory_bytes) {
    if (!ctx || !filename || ctx->mpi_rank != 0) {
        return -1;
    }
    
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        fprintf(stderr, "ERROR: Cannot open file %s for writing\n", filename);
        return -1;
    }
    
    const micro_bench_results_t *results = &ctx->global_results;
    double memory_mb = queue_memory_bytes / (1024.0 * 1024.0);
    
    // Write summary
    fprintf(fp, "# Micro Benchmark Results\n");
    fprintf(fp, "# Test: %s\n", ctx->config.test_name);
    fprintf(fp, "# Consumers: %d, Ops per consumer: %d\n", 
            results->num_consumers, ctx->config.ops_per_consumer);
    fprintf(fp, "# Memory Usage: %.2f MB\n", memory_mb);
    fprintf(fp, "\n");
    
    fprintf(fp, "Metric,Value,Unit\n");
    fprintf(fp, "Total Operations,%ld,operations\n", results->total_operations);
    fprintf(fp, "Total Time,%.6f,seconds\n", results->total_time_sec);
    fprintf(fp, "Total Throughput,%.2f,ops/sec\n", results->total_throughput_ops_per_sec);
    fprintf(fp, "Avg Consumer Time,%.6f,seconds\n", results->avg_consumer_time_sec);
    fprintf(fp, "Min Consumer Time,%.6f,seconds\n", results->min_consumer_time_sec);
    fprintf(fp, "Max Consumer Time,%.6f,seconds\n", results->max_consumer_time_sec);
    fprintf(fp, "Std Dev Consumer Time,%.6f,seconds\n", results->std_dev_consumer_time_sec);
    fprintf(fp, "Memory Usage,%.2f,MB\n", memory_mb);
    
    // Write per-process details if available
    if (results->process_results) {
        fprintf(fp, "\n# Per-Process Details\n");
        fprintf(fp, "Rank,Role,Operations,Time_sec,Throughput_ops_per_sec,Start_sec,Finish_sec\n");
        
        // Write producer (rank 0)
        if (results->process_results[0].operations_completed > 0) {
            micro_process_result_t *pr = &results->process_results[0];
            fprintf(fp, "%d,Producer,%ld,%.6f,%.2f,%.6f,%.6f\n",
                   pr->rank,
                   pr->operations_completed,
                   pr->execution_time_sec,
                   pr->throughput_ops_per_sec,
                   pr->start_time_sec,
                   pr->finish_time_sec);
        }
        
        // Write consumers
        for (int i = 1; i < ctx->mpi_size; i++) {
            micro_process_result_t *pr = &results->process_results[i];
            fprintf(fp, "%d,Consumer,%ld,%.6f,%.2f,%.6f,%.6f\n",
                   pr->rank,
                   pr->operations_completed,
                   pr->execution_time_sec,
                   pr->throughput_ops_per_sec,
                   pr->start_time_sec,
                   pr->finish_time_sec);
        }
    }
    
    fclose(fp);
    printf("Results exported to: %s\n", filename);
    
    return 0;
}

/**
 * Cleanup and free resources
 */
void micro_bench_cleanup(micro_bench_ctx_t *ctx) {
    if (!ctx) {
        return;
    }
    
    if (ctx->global_results.process_results) {
        free(ctx->global_results.process_results);
        ctx->global_results.process_results = NULL;
    }
    
    memset(ctx, 0, sizeof(micro_bench_ctx_t));
}

/**
 * Helper: Print separator line
 */
static void print_separator(void) {
    printf("================================================================\n");
}

/**
 * Helper: Calculate standard deviation
 */
static double calculate_std_dev(double *values, int count, double mean) {
    if (count <= 1) {
        return 0.0;
    }
    
    double sum_sq_diff = 0.0;
    for (int i = 0; i < count; i++) {
        double diff = values[i] - mean;
        sum_sq_diff += diff * diff;
    }
    
    return sqrt(sum_sq_diff / (count - 1));
}
