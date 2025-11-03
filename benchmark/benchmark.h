#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * Benchmark Library for SPMC Queue Performance Testing
 * 
 * This library provides comprehensive benchmarking capabilities for:
 * - Throughput measurement (items/second)
 * - Latency analysis (enqueue/dequeue times)
 * - Scalability testing (different process counts)
 * - Load balancing analysis
 * - Memory usage tracking
 */

// Benchmark configuration structure
typedef struct {
    int num_items;              // Total items to process
    int num_producers;          // Number of producer processes
    int num_consumers;          // Number of consumer processes
    int producer_delay_us;      // Delay between enqueue operations (microseconds)
    int consumer_delay_us;      // Delay between dequeue operations (microseconds)
    int warmup_items;           // Items to produce during warmup
    int test_duration_sec;      // Maximum test duration in seconds
    int enable_latency_tracking; // Enable detailed latency measurements
    int enable_memory_tracking;  // Enable memory usage tracking
    char test_name[128];        // Name of the benchmark test
} benchmark_config_t;

// Performance metrics structure
typedef struct {
    double total_time_sec;      // Total execution time
    double enqueue_throughput_items_per_sec; // Items enqueued per second
    double dequeue_throughput_items_per_sec; // Items dequeued per second
    double avg_enqueue_latency_us;   // Average enqueue latency
    double avg_dequeue_latency_us;   // Average dequeue latency
    double min_enqueue_latency_us;   // Minimum enqueue latency
    double max_enqueue_latency_us;   // Maximum enqueue latency
    double min_dequeue_latency_us;   // Minimum dequeue latency
    double max_dequeue_latency_us;   // Maximum dequeue latency
    long total_items_produced;      // Total items enqueued
    long total_items_consumed;      // Total items dequeued
    long memory_peak_kb;            // Peak memory usage
    long queue_capacity_bytes;
    int load_balance_score;         // Load balancing effectiveness (0-100)
} benchmark_results_t;

// Per-process statistics
typedef struct {
    int rank;
    long items_produced;
    long items_consumed;
    double total_enqueue_time_us;
    double total_dequeue_time_us;
    double min_enqueue_latency_us;
    double max_enqueue_latency_us;
    double min_dequeue_latency_us;
    double max_dequeue_latency_us;
    long memory_usage_kb;
    double enqueue_finish_time_sec;  // Wall-clock time when this process finished enqueuing
    double dequeue_finish_time_sec;  // Wall-clock time when this process finished dequeuing
} process_stats_t;

// Benchmark context
typedef struct {
    void *queue;                    // Generic queue pointer
    benchmark_config_t config;
    benchmark_results_t results;
    process_stats_t local_stats;
    struct timeval start_time;
    struct timeval end_time;
    double *latency_samples;
    int num_latency_samples;
    int max_latency_samples;
    int is_producer;
    int mpi_rank;
    int mpi_size;
} benchmark_ctx_t;

// Function declarations

/**
 * Initialize benchmark context with configuration
 */
int benchmark_init(benchmark_ctx_t *ctx, void *queue, 
                   const benchmark_config_t *config, int is_producer, 
                   int mpi_rank, int mpi_size);

/**
 * Create predefined benchmark configurations
 */
benchmark_config_t benchmark_config_quick_test(int num_items);
benchmark_config_t benchmark_config_throughput_test(int num_items);
benchmark_config_t benchmark_config_latency_test(int num_items);
benchmark_config_t benchmark_config_scalability_test(int num_items, int num_processes);
benchmark_config_t benchmark_config_stress_test(int duration_sec);

/**
 * Start benchmark timing
 */
void benchmark_start(benchmark_ctx_t *ctx);

/**
 * Record enqueue operation timing
 */
void benchmark_record_enqueue(benchmark_ctx_t *ctx, double latency_us, int success);

/**
 * Record dequeue operation timing
 */
void benchmark_record_dequeue(benchmark_ctx_t *ctx, double latency_us, int count);

/**
 * Record when producer finished all enqueue operations
 */
void benchmark_record_producer_finish(benchmark_ctx_t *ctx);

/**
 * Record when consumer finished all dequeue operations
 */
void benchmark_record_consumer_finish(benchmark_ctx_t *ctx);

/**
 * Stop benchmark and calculate results
 */
void benchmark_stop(benchmark_ctx_t *ctx);

/**
 * Aggregate results from all processes using MPI
 */
int benchmark_aggregate_results(benchmark_ctx_t *ctx, void *mpi_comm);

/**
 * Print comprehensive benchmark report
 */
void benchmark_print_report(const benchmark_ctx_t *ctx);

/**
 * Export results to CSV format
 */
int benchmark_export_csv(const benchmark_ctx_t *ctx, const char *filename);

/**
 * Get current time in microseconds
 */
double benchmark_get_time_us(void);

/**
 * Get memory usage in KB
 */
long benchmark_get_memory_usage_kb(void);

/**
 * Calculate load balance score (0-100, 100 = perfect balance)
 */
int benchmark_calculate_load_balance(const process_stats_t *stats, int num_processes);

/**
 * Calculate statistics from latency samples
 */
void benchmark_calculate_latency_stats(benchmark_ctx_t *ctx);

/**
 * Cleanup benchmark resources
 */
void benchmark_cleanup(benchmark_ctx_t *ctx);

// Utility macros
#define BENCHMARK_TIME_START() benchmark_get_time_us()
#define BENCHMARK_TIME_END(start) (benchmark_get_time_us() - (start))

#define BENCHMARK_RECORD_ENQUEUE(ctx, code) do { \
    double start_time = BENCHMARK_TIME_START(); \
    int result = (code); \
    double latency = BENCHMARK_TIME_END(start_time); \
    benchmark_record_enqueue(ctx, latency, result == 0); \
} while(0)

#define BENCHMARK_RECORD_DEQUEUE(ctx, code) do { \
    double start_time = BENCHMARK_TIME_START(); \
    int result = (code); \
    double latency = BENCHMARK_TIME_END(start_time); \
    benchmark_record_dequeue(ctx, latency, result); \
} while(0)

// Predefined test configurations
#define BENCHMARK_QUICK_TEST        {1000, 1, 2, 10000, 20000, 100, 30, 0, 0, "Quick Test"}
#define BENCHMARK_THROUGHPUT_TEST   {10000, 1, 4, 1000, 5000, 500, 60, 0, 0, "Throughput Test"}
#define BENCHMARK_LATENCY_TEST      {5000, 1, 2, 5000, 10000, 200, 45, 1, 0, "Latency Test"}
#define BENCHMARK_SCALABILITY_TEST  {20000, 1, 8, 2000, 8000, 1000, 120, 0, 1, "Scalability Test"}
#define BENCHMARK_STRESS_TEST       {100000, 1, 6, 500, 2000, 2000, 120, 1, 1, "Stress Test"}

#endif // BENCHMARK_H
