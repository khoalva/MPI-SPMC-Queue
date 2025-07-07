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
void benchmark_record_dequeue(benchmark_ctx_t *ctx, double latency_us, int success) {
    if (!ctx) return;
    
    process_stats_t *stats = &ctx->local_stats;
    
    stats->total_dequeue_time_us += latency_us;
    
    if (success) {
        stats->items_consumed++;
        
        if (stats->items_consumed == 1) {
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
 * Stop benchmark and calculate results
 */
void benchmark_stop(benchmark_ctx_t *ctx) {
    if (!ctx) return;
    
    gettimeofday(&ctx->end_time, NULL);
    
    // Calculate total execution time
    double elapsed_sec = (ctx->end_time.tv_sec - ctx->start_time.tv_sec) + 
                        (ctx->end_time.tv_usec - ctx->start_time.tv_usec) / 1000000.0;
    
    ctx->results.total_time_sec = elapsed_sec;
    
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
    
    // Aggregate total items produced and consumed
    long local_produced = ctx->local_stats.items_produced;
    long local_consumed = ctx->local_stats.items_consumed;
    
    MPI_Allreduce(&local_produced, &ctx->results.total_items_produced, 
                  1, MPI_LONG, MPI_SUM, comm);
    MPI_Allreduce(&local_consumed, &ctx->results.total_items_consumed, 
                  1, MPI_LONG, MPI_SUM, comm);
    
    // Calculate throughput
    if (ctx->results.total_time_sec > 0) {
        ctx->results.throughput_items_per_sec = 
            ctx->results.total_items_consumed / ctx->results.total_time_sec;
    }
    
    // Aggregate latency statistics
    double max_enqueue_latency, max_dequeue_latency;
    MPI_Allreduce(&ctx->results.max_enqueue_latency_us, &max_enqueue_latency, 
                  1, MPI_DOUBLE, MPI_MAX, comm);
    MPI_Allreduce(&ctx->results.max_dequeue_latency_us, &max_dequeue_latency, 
                  1, MPI_DOUBLE, MPI_MAX, comm);
    
    ctx->results.max_enqueue_latency_us = max_enqueue_latency;
    ctx->results.max_dequeue_latency_us = max_dequeue_latency;
    
    // Aggregate memory usage
    if (ctx->config.enable_memory_tracking) {
        long max_memory;
        MPI_Allreduce(&ctx->results.memory_peak_kb, &max_memory, 
                      1, MPI_LONG, MPI_MAX, comm);
        ctx->results.memory_peak_kb = max_memory;
    }
    
    // Calculate load balance score
    process_stats_t *all_stats = NULL;
    if (ctx->mpi_rank == 0) {
        all_stats = malloc(ctx->mpi_size * sizeof(process_stats_t));
    }
    
    MPI_Gather(&ctx->local_stats, sizeof(process_stats_t), MPI_BYTE,
               all_stats, sizeof(process_stats_t), MPI_BYTE, 0, comm);
    
    if (ctx->mpi_rank == 0 && all_stats) {
        ctx->results.load_balance_score = 
            benchmark_calculate_load_balance(all_stats, ctx->mpi_size);
        free(all_stats);
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
    printf("  Throughput:              %.2f items/second\\n", ctx->results.throughput_items_per_sec);
    printf("  Load balance score:       %d/100\\n", ctx->results.load_balance_score);
    
    if (ctx->results.avg_enqueue_latency_us > 0) {
        printf("\\nEnqueue Latency:\\n");
        printf("  Average:                 %.2f μs\\n", ctx->results.avg_enqueue_latency_us);
        printf("  Maximum:                 %.2f μs\\n", ctx->results.max_enqueue_latency_us);
    }
    
    if (ctx->results.avg_dequeue_latency_us > 0) {
        printf("\\nDequeue Latency:\\n");
        printf("  Average:                 %.2f μs\\n", ctx->results.avg_dequeue_latency_us);
        printf("  Maximum:                 %.2f μs\\n", ctx->results.max_dequeue_latency_us);
    }
    
    if (ctx->config.enable_memory_tracking) {
        printf("\\nMemory Usage:\\n");
        printf("  Peak memory:             %ld KB\\n", ctx->results.memory_peak_kb);
    }
    
    benchmark_print_separator();
    printf("\\n");
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
    
    // Write CSV header
    fprintf(fp, "Test_Name,MPI_Size,Total_Time_Sec,Items_Produced,Items_Consumed,");
    fprintf(fp, "Throughput_Items_Per_Sec,Avg_Enqueue_Latency_Us,Max_Enqueue_Latency_Us,");
    fprintf(fp, "Avg_Dequeue_Latency_Us,Max_Dequeue_Latency_Us,Memory_Peak_KB,Load_Balance_Score\n");
    
    // Write data with better formatting
    fprintf(fp, "\"%s\",%d,%.3f,%ld,%ld,%.2f,%.2f,%.2f,%.2f,%.2f,%ld,%d\n",
            ctx->config.test_name, ctx->mpi_size, ctx->results.total_time_sec,
            ctx->results.total_items_produced, ctx->results.total_items_consumed,
            ctx->results.throughput_items_per_sec, ctx->results.avg_enqueue_latency_us,
            ctx->results.max_enqueue_latency_us, ctx->results.avg_dequeue_latency_us,
            ctx->results.max_dequeue_latency_us, ctx->results.memory_peak_kb,
            ctx->results.load_balance_score);
    
    fclose(fp);
    return 0;
}

/**
 * Export detailed results to readable CSV format with summary
 */
int benchmark_export_csv_detailed(const benchmark_ctx_t *ctx, const char *filename) {
    if (!ctx || !filename || ctx->mpi_rank != 0) return -1;
    
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        perror("Failed to open detailed CSV file");
        return -1;
    }
    
    // Write header with description
    fprintf(fp, "# SPMC Queue Benchmark Results - Detailed Report\n");
    fprintf(fp, "# Generated on: %s", ctime(&(time_t){time(NULL)}));
    fprintf(fp, "# Test: %s\n", ctx->config.test_name);
    fprintf(fp, "# MPI Processes: %d\n", ctx->mpi_size);
    fprintf(fp, "# Items to Process: %d\n", ctx->config.num_items);
    fprintf(fp, "#\n");
    
    // Summary section
    fprintf(fp, "\n=== SUMMARY ===\n");
    fprintf(fp, "Metric,Value,Unit\n");
    fprintf(fp, "Test Name,\"%s\",\n", ctx->config.test_name);
    fprintf(fp, "MPI Processes,%d,processes\n", ctx->mpi_size);
    fprintf(fp, "Total Time,%.3f,seconds\n", ctx->results.total_time_sec);
    fprintf(fp, "Items Produced,%ld,items\n", ctx->results.total_items_produced);
    fprintf(fp, "Items Consumed,%ld,items\n", ctx->results.total_items_consumed);
    fprintf(fp, "Total Throughput,%.2f,items/sec\n", ctx->results.throughput_items_per_sec);
    fprintf(fp, "Avg Enqueue Latency,%.2f,microseconds\n", ctx->results.avg_enqueue_latency_us);
    fprintf(fp, "Max Enqueue Latency,%.2f,microseconds\n", ctx->results.max_enqueue_latency_us);
    fprintf(fp, "Avg Dequeue Latency,%.2f,microseconds\n", ctx->results.avg_dequeue_latency_us);
    fprintf(fp, "Max Dequeue Latency,%.2f,microseconds\n", ctx->results.max_dequeue_latency_us);
    fprintf(fp, "Memory Peak,%ld,KB\n", ctx->results.memory_peak_kb);
    fprintf(fp, "Load Balance Score,%d,percent\n", ctx->results.load_balance_score);
    
    // Performance analysis section
    fprintf(fp, "\n=== PERFORMANCE ANALYSIS ===\n");
    fprintf(fp, "Analysis,Value,Interpretation\n");
    
    // Throughput analysis
    if (ctx->results.throughput_items_per_sec > 5000) {
        fprintf(fp, "Throughput,%.2f items/sec,Excellent\n", ctx->results.throughput_items_per_sec);
    } else if (ctx->results.throughput_items_per_sec > 2000) {
        fprintf(fp, "Throughput,%.2f items/sec,Good\n", ctx->results.throughput_items_per_sec);
    } else if (ctx->results.throughput_items_per_sec > 1000) {
        fprintf(fp, "Throughput,%.2f items/sec,Average\n", ctx->results.throughput_items_per_sec);
    } else {
        fprintf(fp, "Throughput,%.2f items/sec,Needs Improvement\n", ctx->results.throughput_items_per_sec);
    }
    
    // Latency analysis
    if (ctx->results.avg_enqueue_latency_us < 10) {
        fprintf(fp, "Enqueue Latency,%.2f μs,Excellent\n", ctx->results.avg_enqueue_latency_us);
    } else if (ctx->results.avg_enqueue_latency_us < 50) {
        fprintf(fp, "Enqueue Latency,%.2f μs,Good\n", ctx->results.avg_enqueue_latency_us);
    } else if (ctx->results.avg_enqueue_latency_us < 100) {
        fprintf(fp, "Enqueue Latency,%.2f μs,Average\n", ctx->results.avg_enqueue_latency_us);
    } else {
        fprintf(fp, "Enqueue Latency,%.2f μs,High\n", ctx->results.avg_enqueue_latency_us);
    }
    
    // Load balance analysis
    if (ctx->results.load_balance_score >= 95) {
        fprintf(fp, "Load Balance,%d%%,Excellent Distribution\n", ctx->results.load_balance_score);
    } else if (ctx->results.load_balance_score >= 85) {
        fprintf(fp, "Load Balance,%d%%,Good Distribution\n", ctx->results.load_balance_score);
    } else if (ctx->results.load_balance_score >= 70) {
        fprintf(fp, "Load Balance,%d%%,Moderate Imbalance\n", ctx->results.load_balance_score);
    } else {
        fprintf(fp, "Load Balance,%d%%,Poor Distribution\n", ctx->results.load_balance_score);
    }
    
    // Configuration details
    fprintf(fp, "\n=== CONFIGURATION ===\n");
    fprintf(fp, "Parameter,Value,Unit\n");
    fprintf(fp, "Items to Process,%d,items\n", ctx->config.num_items);
    fprintf(fp, "Producers,%d,processes\n", ctx->config.num_producers);
    fprintf(fp, "Consumers,%d,processes\n", ctx->config.num_consumers);
    fprintf(fp, "Producer Delay,%d,microseconds\n", ctx->config.producer_delay_us);
    fprintf(fp, "Consumer Delay,%d,microseconds\n", ctx->config.consumer_delay_us);
    fprintf(fp, "Warmup Items,%d,items\n", ctx->config.warmup_items);
    fprintf(fp, "Max Duration,%d,seconds\n", ctx->config.test_duration_sec);
    fprintf(fp, "Latency Tracking,%s,\n", ctx->config.enable_latency_tracking ? "Enabled" : "Disabled");
    fprintf(fp, "Memory Tracking,%s,\n", ctx->config.enable_memory_tracking ? "Enabled" : "Disabled");
    
    // Recommendations
    fprintf(fp, "\n=== RECOMMENDATIONS ===\n");
    fprintf(fp, "Area,Recommendation,Reason\n");
    
    if (ctx->results.throughput_items_per_sec < 2000) {
        fprintf(fp, "Performance,Consider optimizing queue operations,Low throughput detected\n");
    }
    
    if (ctx->results.avg_enqueue_latency_us > 100) {
        fprintf(fp, "Latency,Review enqueue implementation,High latency detected\n");
    }
    
    if (ctx->results.load_balance_score < 85) {
        fprintf(fp, "Load Balancing,Investigate consumer distribution,Uneven workload detected\n");
    }
    
    if (ctx->mpi_size == 2) {
        fprintf(fp, "Scalability,Try with more processes,Limited parallelism with 2 processes\n");
    }
    
    fprintf(fp, "\n# End of detailed report\n");
    
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
