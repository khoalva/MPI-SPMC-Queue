#include "benchmark.h"
#include <mpi.h>
#include <math.h>
#include <sys/resource.h>
#include <errno.h>
#include <time.h>

/**
 * Benchmark Library Implementation for SPMC Queue Performance Testing
 */

// Internal helper functions
static void benchmark_init_stats(process_stats_t *stats, int rank);
// static double benchmark_calculate_percentile(double *samples, int count, double percentile);
static void benchmark_print_separator(void);

/**
 * Initialize benchmark context with configuration
 */
int benchmark_init(benchmark_ctx_t *ctx, void *queue, 
                   const benchmark_config_t *config, int is_producer, 
                   int mpi_rank, int mpi_size) {
    if (!ctx || !queue || !config) {
        return -1;
    }
    
    memset(ctx, 0, sizeof(benchmark_ctx_t));
    
    ctx->queue = queue;
    ctx->config = *config;
    ctx->is_producer = is_producer;
    ctx->mpi_rank = mpi_rank;
    ctx->mpi_size = mpi_size;
    
    // Initialize local statistics
    benchmark_init_stats(&ctx->local_stats, mpi_rank);
    
    // Allocate latency samples array if tracking is enabled
    if (config->enable_latency_tracking) {
        ctx->max_latency_samples = config->num_items * 2; // Conservative estimate
        ctx->latency_samples = calloc(ctx->max_latency_samples, sizeof(double));
        if (!ctx->latency_samples) {
            return -1;
        }
    }
    
    return 0;
}

/**
 * Create predefined benchmark configurations
 */
benchmark_config_t benchmark_config_quick_test(int num_items) {
    benchmark_config_t config = BENCHMARK_QUICK_TEST;
    config.num_items = num_items;
    return config;
}

benchmark_config_t benchmark_config_throughput_test(int num_items) {
    benchmark_config_t config = BENCHMARK_THROUGHPUT_TEST;
    config.num_items = num_items;
    return config;
}

benchmark_config_t benchmark_config_latency_test(int num_items) {
    benchmark_config_t config = BENCHMARK_LATENCY_TEST;
    config.num_items = num_items;
    config.enable_latency_tracking = 1;
    return config;
}

benchmark_config_t benchmark_config_scalability_test(int num_items, int num_processes) {
    benchmark_config_t config = BENCHMARK_SCALABILITY_TEST;
    config.num_items = num_items;
    config.num_consumers = num_processes - 1; // One producer, rest consumers
    config.enable_memory_tracking = 1;
    return config;
}

benchmark_config_t benchmark_config_stress_test(int duration_sec) {
    benchmark_config_t config = BENCHMARK_STRESS_TEST;
    config.test_duration_sec = duration_sec;
    config.enable_latency_tracking = 1;
    config.enable_memory_tracking = 1;
    return config;
}

/**
 * Start benchmark timing
 */
void benchmark_start(benchmark_ctx_t *ctx) {
    if (!ctx) return;
    
    gettimeofday(&ctx->start_time, NULL);
    
    if (ctx->mpi_rank == 0) {
        printf("\\n=== Benchmark: %s ===\\n", ctx->config.test_name);
        printf("Configuration:\\n");
        printf("  Items to process: %d\\n", ctx->config.num_items);
        printf("  Producers: %d\\n", ctx->config.num_producers);
        printf("  Consumers: %d\\n", ctx->config.num_consumers);
        printf("  MPI Processes: %d\\n", ctx->mpi_size);
        printf("  Latency tracking: %s\\n", ctx->config.enable_latency_tracking ? "Enabled" : "Disabled");
        printf("  Memory tracking: %s\\n", ctx->config.enable_memory_tracking ? "Enabled" : "Disabled");
        benchmark_print_separator();
    }
}

/**
 * Record enqueue operation timing
 */
void benchmark_record_enqueue(benchmark_ctx_t *ctx, double latency_us, int success) {
    if (!ctx) return;
    
    process_stats_t *stats = &ctx->local_stats;
    
    if (success) {
        stats->items_produced++;
        stats->total_enqueue_time_us += latency_us;
        
        if (stats->items_produced == 1) {
            stats->min_enqueue_latency_us = latency_us;
            stats->max_enqueue_latency_us = latency_us;
        } else {
            if (latency_us < stats->min_enqueue_latency_us) {
                stats->min_enqueue_latency_us = latency_us;
            }
            if (latency_us > stats->max_enqueue_latency_us) {
                stats->max_enqueue_latency_us = latency_us;
            }
        }
        
        // Store latency sample if tracking is enabled
        if (ctx->config.enable_latency_tracking && 
            ctx->num_latency_samples < ctx->max_latency_samples) {
            ctx->latency_samples[ctx->num_latency_samples++] = latency_us;
        }
    }
}

/**
 * Record dequeue operation timing
 */
void benchmark_record_dequeue(benchmark_ctx_t *ctx, double latency_us, int count) {
    if (!ctx) return;
    
    process_stats_t *stats = &ctx->local_stats;
    
    stats->total_dequeue_time_us += latency_us;
    
    if (count > 0) {
        stats->items_consumed += count;  // Add the actual count of items dequeued
        
        if (stats->items_consumed == count) {  // First dequeue operation
            stats->min_dequeue_latency_us = latency_us;
            stats->max_dequeue_latency_us = latency_us;
        } else {
            if (latency_us < stats->min_dequeue_latency_us) {
                stats->min_dequeue_latency_us = latency_us;
            }
            if (latency_us > stats->max_dequeue_latency_us) {
                stats->max_dequeue_latency_us = latency_us;
            }
        }
        
        // Store latency sample if tracking is enabled
        if (ctx->config.enable_latency_tracking && 
            ctx->num_latency_samples < ctx->max_latency_samples) {
            ctx->latency_samples[ctx->num_latency_samples++] = latency_us;
        }
    }
}

/**
 * Record when producer finished all enqueue operations
 */
void benchmark_record_producer_finish(benchmark_ctx_t *ctx) {
    if (!ctx) return;
    
    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    
    ctx->local_stats.enqueue_finish_time_sec = 
        (current_time.tv_sec - ctx->start_time.tv_sec) + 
        (current_time.tv_usec - ctx->start_time.tv_usec) / 1000000.0;
}

/**
 * Record when consumer finished all dequeue operations
 */
void benchmark_record_consumer_finish(benchmark_ctx_t *ctx) {
    if (!ctx) return;
    
    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    
    ctx->local_stats.dequeue_finish_time_sec = 
        (current_time.tv_sec - ctx->start_time.tv_sec) + 
        (current_time.tv_usec - ctx->start_time.tv_usec) / 1000000.0;
}

/**
 * Stop benchmark and calculate results
 */
void benchmark_stop(benchmark_ctx_t *ctx) {
    if (!ctx) return;
    
    gettimeofday(&ctx->end_time, NULL);
    
    // Calculate total execution time
    double elapsed_sec = (ctx->end_time.tv_sec - ctx->start_time.tv_sec) + 
                        (ctx->end_time.tv_usec - ctx->start_time.tv_usec) / 1000000.0;
    
    ctx->results.total_time_sec = elapsed_sec;
    
    // Initialize results to 0 to prevent MPI issues with uninitialized values
    ctx->results.memory_peak_kb = 0;
    ctx->results.enqueue_throughput_items_per_sec = 0.0;
    ctx->results.dequeue_throughput_items_per_sec = 0.0;
    
    // Calculate local statistics
    process_stats_t *stats = &ctx->local_stats;
    
    if (stats->items_produced > 0) {
        ctx->results.avg_enqueue_latency_us = stats->total_enqueue_time_us / stats->items_produced;
        ctx->results.max_enqueue_latency_us = stats->max_enqueue_latency_us;
    }
    
    if (stats->items_consumed > 0) {
        ctx->results.avg_dequeue_latency_us = stats->total_dequeue_time_us / stats->items_consumed;
        ctx->results.max_dequeue_latency_us = stats->max_dequeue_latency_us;
    }
    
    // Get memory usage if tracking is enabled
    if (ctx->config.enable_memory_tracking) {
        ctx->results.memory_peak_kb = benchmark_get_memory_usage_kb();
        stats->memory_usage_kb = ctx->results.memory_peak_kb;
    }
    
    // Calculate latency statistics from samples
    if (ctx->config.enable_latency_tracking) {
        benchmark_calculate_latency_stats(ctx);
    }
}

/**
 * Aggregate results from all processes using MPI
 */
int benchmark_aggregate_results(benchmark_ctx_t *ctx, void *mpi_comm) {
    if (!ctx || !mpi_comm) return -1;
    
    MPI_Comm comm = *((MPI_Comm*)mpi_comm);
    
    // First, synchronize timing across all processes
    // Get the earliest start time and latest end time across all processes
    double local_start_time = ctx->start_time.tv_sec + ctx->start_time.tv_usec / 1000000.0;
    double local_end_time = ctx->end_time.tv_sec + ctx->end_time.tv_usec / 1000000.0;
    
    double global_start_time, global_end_time;
    MPI_Allreduce(&local_start_time, &global_start_time, 1, MPI_DOUBLE, MPI_MIN, comm);
    MPI_Allreduce(&local_end_time, &global_end_time, 1, MPI_DOUBLE, MPI_MAX, comm);
    
    // Calculate the actual benchmark duration across all processes
    double actual_benchmark_time = global_end_time - global_start_time;
    ctx->results.total_time_sec = actual_benchmark_time;
    
    // Debug timing information
    if (ctx->mpi_rank == 0) {
        printf("\\nTiming Debug Information:\\n");
        printf("  Global start time:       %.6f seconds\\n", global_start_time);
        printf("  Global end time:         %.6f seconds\\n", global_end_time);
        printf("  Actual benchmark time:   %.6f seconds\\n", actual_benchmark_time);
        printf("  Local execution time:    %.6f seconds\\n", 
               (ctx->end_time.tv_sec - ctx->start_time.tv_sec) + 
               (ctx->end_time.tv_usec - ctx->start_time.tv_usec) / 1000000.0);
    }
    
    // Aggregate total items produced and consumed
    long local_produced = ctx->local_stats.items_produced;
    long local_consumed = ctx->local_stats.items_consumed;
    
    // For SPMC: Only sum items_produced from producers, sum items_consumed from all consumers
    // But we need to be careful - in proper SPMC, total consumed should never exceed total produced
    MPI_Allreduce(&local_produced, &ctx->results.total_items_produced, 
                  1, MPI_LONG, MPI_SUM, comm);
    MPI_Allreduce(&local_consumed, &ctx->results.total_items_consumed, 
                  1, MPI_LONG, MPI_SUM, comm);
    
    // Validation: Ensure total_items_consumed doesn't exceed total_items_produced
    if (ctx->results.total_items_consumed > ctx->results.total_items_produced) {
        if (ctx->mpi_rank == 0) {
            printf("WARNING: Items consumed (%ld) exceeds items produced (%ld)\n", 
                   ctx->results.total_items_consumed, ctx->results.total_items_produced);
            printf("This indicates a counting error or race condition in the benchmark.\n");
        }
        // Cap consumed items to produced items for consistency
        ctx->results.total_items_consumed = ctx->results.total_items_produced;
    }
    
    // Calculate throughput based on actual finish times
    // Find the maximum finish time for producers and consumers across all processes
    double local_enqueue_finish_time = ctx->local_stats.enqueue_finish_time_sec;
    double local_dequeue_finish_time = ctx->local_stats.dequeue_finish_time_sec;
    
    // If this process didn't do enqueue/dequeue, set to 0 (will be ignored in MAX operation)
    if (ctx->local_stats.items_produced == 0) local_enqueue_finish_time = 0.0;
    if (ctx->local_stats.items_consumed == 0) local_dequeue_finish_time = 0.0;
    
    double max_enqueue_finish_time, max_dequeue_finish_time;
    MPI_Allreduce(&local_enqueue_finish_time, &max_enqueue_finish_time, 1, MPI_DOUBLE, MPI_MAX, comm);
    MPI_Allreduce(&local_dequeue_finish_time, &max_dequeue_finish_time, 1, MPI_DOUBLE, MPI_MAX, comm);
    
    // Calculate throughput = Total items / Time to complete all operations
    ctx->results.enqueue_throughput_items_per_sec = 0.0;
    ctx->results.dequeue_throughput_items_per_sec = 0.0;
    
    if (max_enqueue_finish_time > 0) {
        ctx->results.enqueue_throughput_items_per_sec = 
            ctx->results.total_items_produced / max_enqueue_finish_time;
    } else {
        // Advanced fallback: Estimate throughput based on operation patterns
        // Calculate average operation time and estimate effective completion time
        
        // Aggregate CPU times to estimate relative performance
        double local_enqueue_time_sec = ctx->local_stats.total_enqueue_time_us / 1000000.0;
        double local_dequeue_time_sec = ctx->local_stats.total_dequeue_time_us / 1000000.0;
        
        double total_enqueue_cpu_time, total_dequeue_cpu_time;
        MPI_Allreduce(&local_enqueue_time_sec, &total_enqueue_cpu_time, 1, MPI_DOUBLE, MPI_SUM, comm);
        MPI_Allreduce(&local_dequeue_time_sec, &total_dequeue_cpu_time, 1, MPI_DOUBLE, MPI_SUM, comm);
        
        if (total_enqueue_cpu_time > 0) {
            // Estimate: Enqueue throughput based on average operation efficiency
            ctx->results.enqueue_throughput_items_per_sec = 
                ctx->results.total_items_produced / total_enqueue_cpu_time;
        } else if (ctx->results.total_time_sec > 0) {
            // Final fallback to wall-clock time
            ctx->results.enqueue_throughput_items_per_sec = 
                ctx->results.total_items_produced / ctx->results.total_time_sec;
        }
    }
    
    if (max_dequeue_finish_time > 0) {
        ctx->results.dequeue_throughput_items_per_sec = 
            ctx->results.total_items_consumed / max_dequeue_finish_time;
    } else {
        // Advanced fallback: Same approach for dequeue
        double local_enqueue_time_sec = ctx->local_stats.total_enqueue_time_us / 1000000.0;
        double local_dequeue_time_sec = ctx->local_stats.total_dequeue_time_us / 1000000.0;
        
        double total_enqueue_cpu_time, total_dequeue_cpu_time;
        MPI_Allreduce(&local_enqueue_time_sec, &total_enqueue_cpu_time, 1, MPI_DOUBLE, MPI_SUM, comm);
        MPI_Allreduce(&local_dequeue_time_sec, &total_dequeue_cpu_time, 1, MPI_DOUBLE, MPI_SUM, comm);
        
        if (total_dequeue_cpu_time > 0) {
            // Estimate: Dequeue throughput based on average operation efficiency
            ctx->results.dequeue_throughput_items_per_sec = 
                ctx->results.total_items_consumed / total_dequeue_cpu_time;
        } else if (ctx->results.total_time_sec > 0) {
            // Final fallback to wall-clock time
            ctx->results.dequeue_throughput_items_per_sec = 
                ctx->results.total_items_consumed / ctx->results.total_time_sec;
        }
    }
    
    // Debug: Print detailed throughput analysis
    if (ctx->mpi_rank == 0) {
        printf("\\nThroughput Analysis Debug:\\n");
        printf("  Enqueue finish time:     %.6f seconds (max across all producers)\\n", max_enqueue_finish_time);
        printf("  Dequeue finish time:     %.6f seconds (max across all consumers)\\n", max_dequeue_finish_time);
        
        const char *enqueue_method = (max_enqueue_finish_time > 0) ? "finish-time" : "CPU-time estimate";
        const char *dequeue_method = (max_dequeue_finish_time > 0) ? "finish-time" : "CPU-time estimate";
        
        printf("  Enqueue throughput:      %.2f items/sec (%s method)\\n", 
               ctx->results.enqueue_throughput_items_per_sec, enqueue_method);
        printf("  Dequeue throughput:      %.2f items/sec (%s method)\\n", 
               ctx->results.dequeue_throughput_items_per_sec, dequeue_method);
               
        if (max_enqueue_finish_time == 0 || max_dequeue_finish_time == 0) {
            printf("  WARNING: Finish times not recorded. Using CPU-time estimates.\\n");
            printf("  For accurate throughput, call benchmark_record_*_finish() functions.\\n");
        }
    }
    
    // Debug: Print per-process statistics on rank 0
    if (ctx->mpi_rank == 0) {
        printf("\\nDebug - Per-process statistics:\\n");
        printf("  Rank %d (this): produced=%ld, consumed=%ld\\n", 
               ctx->mpi_rank, ctx->local_stats.items_produced, ctx->local_stats.items_consumed);
    }
    
    // Gather individual process statistics for debugging
    process_stats_t *all_stats = NULL;
    if (ctx->mpi_rank == 0) {
        all_stats = malloc(ctx->mpi_size * sizeof(process_stats_t));
    }
    
    MPI_Gather(&ctx->local_stats, sizeof(process_stats_t), MPI_BYTE,
               all_stats, sizeof(process_stats_t), MPI_BYTE, 0, comm);
    
    if (ctx->mpi_rank == 0 && all_stats) {
        printf("\\nAll processes statistics:\\n");
        long debug_total_produced = 0, debug_total_consumed = 0;
        for (int i = 0; i < ctx->mpi_size; i++) {
            printf("  Rank %d: produced=%ld, consumed=%ld\\n", 
                   all_stats[i].rank, all_stats[i].items_produced, all_stats[i].items_consumed);
            debug_total_produced += all_stats[i].items_produced;
            debug_total_consumed += all_stats[i].items_consumed;
        }
        printf("  DEBUG TOTALS: produced=%ld, consumed=%ld\\n", 
               debug_total_produced, debug_total_consumed);
        
        ctx->results.load_balance_score = 
            benchmark_calculate_load_balance(all_stats, ctx->mpi_size);
        free(all_stats);
    }
    
    // Aggregate latency statistics
    double max_enqueue_latency, max_dequeue_latency;
    double min_enqueue_latency, min_dequeue_latency;
    
    // Get local min/max values
    double local_min_enqueue = ctx->local_stats.min_enqueue_latency_us;
    double local_min_dequeue = ctx->local_stats.min_dequeue_latency_us;
    
    // Handle case where no operations were performed (set to very high value for min)
    if (ctx->local_stats.items_produced == 0) local_min_enqueue = 1e9;
    if (ctx->local_stats.items_consumed == 0) local_min_dequeue = 1e9;
    
    // Aggregate maximum latencies
    MPI_Allreduce(&ctx->results.max_enqueue_latency_us, &max_enqueue_latency, 
                  1, MPI_DOUBLE, MPI_MAX, comm);
    MPI_Allreduce(&ctx->results.max_dequeue_latency_us, &max_dequeue_latency, 
                  1, MPI_DOUBLE, MPI_MAX, comm);
    
    // Aggregate minimum latencies
    MPI_Allreduce(&local_min_enqueue, &min_enqueue_latency, 
                  1, MPI_DOUBLE, MPI_MIN, comm);
    MPI_Allreduce(&local_min_dequeue, &min_dequeue_latency, 
                  1, MPI_DOUBLE, MPI_MIN, comm);
    
    // Aggregate average latencies (weighted by number of operations)
    double local_enqueue_total = ctx->local_stats.total_enqueue_time_us;
    double local_dequeue_total = ctx->local_stats.total_dequeue_time_us;
    long local_enqueue_count = ctx->local_stats.items_produced;
    long local_dequeue_count = ctx->local_stats.items_consumed;
    
    double global_enqueue_total, global_dequeue_total;
    long global_enqueue_count, global_dequeue_count;
    
    MPI_Allreduce(&local_enqueue_total, &global_enqueue_total, 1, MPI_DOUBLE, MPI_SUM, comm);
    MPI_Allreduce(&local_dequeue_total, &global_dequeue_total, 1, MPI_DOUBLE, MPI_SUM, comm);
    MPI_Allreduce(&local_enqueue_count, &global_enqueue_count, 1, MPI_LONG, MPI_SUM, comm);
    MPI_Allreduce(&local_dequeue_count, &global_dequeue_count, 1, MPI_LONG, MPI_SUM, comm);
    
    // Calculate global average latencies
    if (global_enqueue_count > 0) {
        ctx->results.avg_enqueue_latency_us = global_enqueue_total / global_enqueue_count;
    }
    if (global_dequeue_count > 0) {
        ctx->results.avg_dequeue_latency_us = global_dequeue_total / global_dequeue_count;
    }
    
    ctx->results.max_enqueue_latency_us = max_enqueue_latency;
    ctx->results.max_dequeue_latency_us = max_dequeue_latency;
    
    // Set minimum latencies (handle case where no operations occurred)
    ctx->results.min_enqueue_latency_us = (min_enqueue_latency < 1e8) ? min_enqueue_latency : 0.0;
    ctx->results.min_dequeue_latency_us = (min_dequeue_latency < 1e8) ? min_dequeue_latency : 0.0;
    
    // Aggregate memory usage
    if (ctx->config.enable_memory_tracking) {
        long max_memory;
        MPI_Allreduce(&ctx->results.memory_peak_kb, &max_memory, 
                      1, MPI_LONG, MPI_MAX, comm);
        ctx->results.memory_peak_kb = max_memory;
    }
    
    return 0;
}

/**
 * Print comprehensive benchmark report
 */
void benchmark_print_report(const benchmark_ctx_t *ctx) {
    if (!ctx || ctx->mpi_rank != 0) return;
    
    printf("\\n");
    benchmark_print_separator();
    printf("BENCHMARK RESULTS: %s\\n", ctx->config.test_name);
    benchmark_print_separator();
    
    printf("Performance Metrics:\\n");
    printf("  Total execution time:     %.3f seconds\\n", ctx->results.total_time_sec);
    printf("  Total items produced:     %ld\\n", ctx->results.total_items_produced);
    printf("  Total items consumed:     %ld\\n", ctx->results.total_items_consumed);
    
    // Calculate and display overall throughput (total items processed per second)
    double overall_throughput = 0.0;
    if (ctx->results.total_time_sec > 0) {
        // Use the minimum of produced/consumed as the actual throughput
        long effective_items = (ctx->results.total_items_consumed < ctx->results.total_items_produced) 
                              ? ctx->results.total_items_consumed : ctx->results.total_items_produced;
        overall_throughput = effective_items / ctx->results.total_time_sec;
    }
    
    printf("\\nThroughput Analysis:\\n");
    printf("  Overall throughput:      %.2f items/second (effective end-to-end)\\n", overall_throughput);
    printf("  Enqueue throughput:      %.2f items/second (finish-time based)\\n", ctx->results.enqueue_throughput_items_per_sec);
    printf("  Dequeue throughput:      %.2f items/second (finish-time based)\\n", ctx->results.dequeue_throughput_items_per_sec);
    printf("\\nSystem Metrics:\\n");
    printf("  Load balance score:       %d/100\\n", ctx->results.load_balance_score);
    
    printf("\\nEnqueue Latency:\\n");
    printf("  Minimum:                 %.2f μs\\n", ctx->results.min_enqueue_latency_us);
    printf("  Average:                 %.2f μs\\n", ctx->results.avg_enqueue_latency_us);
    printf("  Maximum:                 %.2f μs\\n", ctx->results.max_enqueue_latency_us);
    
    printf("\\nDequeue Latency:\\n");
    printf("  Minimum:                 %.2f μs\\n", ctx->results.min_dequeue_latency_us);
    printf("  Average:                 %.2f μs\\n", ctx->results.avg_dequeue_latency_us);
    printf("  Maximum:                 %.2f μs\\n", ctx->results.max_dequeue_latency_us);
    
    if (ctx->config.enable_memory_tracking) {
        printf("\nMemory Usage:\n");
        printf("  Peak memory:             %ld KB\n", ctx->results.memory_peak_kb);
    }

    // Space complexity
    if (ctx->results.queue_capacity_bytes > 0) {
        printf("\nQueue Space Complexity:\n");
        printf("  Queue capacity (bytes):  %ld\n", ctx->results.queue_capacity_bytes);
    }

    benchmark_print_separator();
    printf("\n");
}

/**
 * Export results to CSV format
 */
int benchmark_export_csv(const benchmark_ctx_t *ctx, const char *filename) {
    if (!ctx || !filename || ctx->mpi_rank != 0) return -1;
    
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        perror("Failed to open CSV file");
        return -1;
    }
    
    // Calculate overall throughput for CSV
    double overall_throughput = 0.0;
    if (ctx->results.total_time_sec > 0) {
        long effective_items = (ctx->results.total_items_consumed < ctx->results.total_items_produced) 
                              ? ctx->results.total_items_consumed : ctx->results.total_items_produced;
        overall_throughput = effective_items / ctx->results.total_time_sec;
    }
    
    // Write CSV header
    fprintf(fp, "Test_Name,MPI_Size,Total_Time_Sec,Items_Produced,Items_Consumed,");
    fprintf(fp, "Overall_Throughput_Items_Per_Sec,Enqueue_Throughput_Items_Per_Sec,Dequeue_Throughput_Items_Per_Sec,");
    fprintf(fp, "Min_Enqueue_Latency_Us,Avg_Enqueue_Latency_Us,Max_Enqueue_Latency_Us,");
    fprintf(fp, "Min_Dequeue_Latency_Us,Avg_Dequeue_Latency_Us,Max_Dequeue_Latency_Us,");
    fprintf(fp, "Memory_Peak_KB,Load_Balance_Score,Queue_Capacity_Bytes\n");

    // Write data with better formatting
    fprintf(fp, "\"%s\",%d,%.3f,%ld,%ld,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%ld,%d,%ld\n",
            ctx->config.test_name, ctx->mpi_size, ctx->results.total_time_sec,
            ctx->results.total_items_produced, ctx->results.total_items_consumed,
            overall_throughput, ctx->results.enqueue_throughput_items_per_sec, ctx->results.dequeue_throughput_items_per_sec, 
            ctx->results.min_enqueue_latency_us, ctx->results.avg_enqueue_latency_us, ctx->results.max_enqueue_latency_us,
            ctx->results.min_dequeue_latency_us, ctx->results.avg_dequeue_latency_us, ctx->results.max_dequeue_latency_us, 
            ctx->results.memory_peak_kb, ctx->results.load_balance_score,
            ctx->results.queue_capacity_bytes);
    
    fclose(fp);
    return 0;
}

/**
 * Get current time in microseconds
 */
double benchmark_get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000000.0 + (double)tv.tv_usec;
}

/**
 * Get memory usage in KB
 */
long benchmark_get_memory_usage_kb(void) {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return usage.ru_maxrss; // maxrss is in KB on Linux
    }
    return 0;
}

/**
 * Calculate load balance score (0-100, 100 = perfect balance)
 */
int benchmark_calculate_load_balance(const process_stats_t *stats, int num_processes) {
    if (!stats || num_processes <= 1) return 100;
    
    // Calculate load balance based on consumed items variance
    long total_consumed = 0;
    long consumer_counts[num_processes];
    int num_consumers = 0;
    
    for (int i = 0; i < num_processes; i++) {
        if (stats[i].items_consumed > 0) {
            consumer_counts[num_consumers] = stats[i].items_consumed;
            total_consumed += stats[i].items_consumed;
            num_consumers++;
        }
    }
    
    if (num_consumers <= 1) return 100;
    
    double mean = (double)total_consumed / num_consumers;
    double variance = 0.0;
    
    for (int i = 0; i < num_consumers; i++) {
        double diff = consumer_counts[i] - mean;
        variance += diff * diff;
    }
    variance /= num_consumers;
    
    double std_dev = sqrt(variance);
    double coefficient_of_variation = (mean > 0) ? (std_dev / mean) : 0.0;
    
    // Convert to score (lower CV = higher score)
    int score = (int)(100.0 * (1.0 - fmin(coefficient_of_variation, 1.0)));
    return fmax(0, fmin(100, score));
}

/**
 * Calculate statistics from latency samples
 */
void benchmark_calculate_latency_stats(benchmark_ctx_t *ctx) {
    if (!ctx || !ctx->latency_samples || ctx->num_latency_samples == 0) return;
    
    double *samples = ctx->latency_samples;
    int count = ctx->num_latency_samples;
    
    // Sort samples for percentile calculations
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (samples[j] > samples[j + 1]) {
                double temp = samples[j];
                samples[j] = samples[j + 1];
                samples[j + 1] = temp;
            }
        }
    }
    
    // Calculate percentiles and store in results
    // You can extend this to calculate 50th, 95th, 99th percentiles etc.
}

/**
 * Cleanup benchmark resources
 */
void benchmark_cleanup(benchmark_ctx_t *ctx) {
    if (!ctx) return;
    
    if (ctx->latency_samples) {
        free(ctx->latency_samples);
        ctx->latency_samples = NULL;
    }
    
    memset(ctx, 0, sizeof(benchmark_ctx_t));
}

// Internal helper functions

static void benchmark_init_stats(process_stats_t *stats, int rank) {
    memset(stats, 0, sizeof(process_stats_t));
    stats->rank = rank;
    stats->min_enqueue_latency_us = 1e9;
    stats->max_enqueue_latency_us = 0.0;
    stats->min_dequeue_latency_us = 1e9;
    stats->max_dequeue_latency_us = 0.0;
    stats->enqueue_finish_time_sec = 0.0;
    stats->dequeue_finish_time_sec = 0.0;
}

// Percentile calculation function (currently unused but available for future extensions)
/* static double benchmark_calculate_percentile(double *samples, int count, double percentile) {
    if (!samples || count == 0) return 0.0;
    
    int index = (int)((percentile / 100.0) * (count - 1));
    return samples[index];
} */

static void benchmark_print_separator(void) {
    printf("================================================================\\n");
}
