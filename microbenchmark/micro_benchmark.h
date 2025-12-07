#ifndef MICRO_BENCHMARK_H
#define MICRO_BENCHMARK_H

#include <sys/time.h>
#include <stddef.h>
#include <mpi.h>

/**
 * Micro Benchmark Library for SPMC Queue Performance Testing
 * 
 * This library implements proper micro benchmarking methodology:
 * - Fixed operations per consumer (e.g., 5000 dequeues)
 * - Warmup phase to prefill queue (num_consumers * ops_per_consumer)
 * - Measure actual consumer execution time (no empty dequeue waits)
 * - High contention testing (queue always has items)
 * - Throughput calculation based on total operations / total time
 * 
 * Advantages over system testing approach:
 * - Measures pure queue performance, not coordination overhead
 * - Eliminates timing artifacts from empty queue waits
 * - Provides consistent, reproducible results
 * - Better reflects real high-contention scenarios
 */

// Fixed operations per consumer (can be configured)
#define DEFAULT_OPS_PER_CONSUMER 1000

// Micro benchmark configuration
typedef struct {
    int num_consumers;              // Number of consumer processes
    int ops_per_consumer;           // Fixed operations each consumer performs
    int enable_verbose;             // Print detailed per-process stats
    char test_name[128];            // Name of the benchmark test
} micro_bench_config_t;

// Per-process timing results
typedef struct {
    int rank;                       // MPI rank
    long operations_completed;      // Actual operations completed (should equal ops_per_consumer)
    double execution_time_sec;      // Time taken by this process
    double throughput_ops_per_sec;  // Local throughput
    double start_time_sec;          // When this process started (relative to benchmark start)
    double finish_time_sec;         // When this process finished (relative to benchmark start)
} micro_process_result_t;

// Aggregated benchmark results
typedef struct {
    double total_time_sec;                  // Total benchmark duration (max finish time)
    double total_throughput_ops_per_sec;    // Total operations / total time
    double avg_consumer_time_sec;           // Average consumer execution time
    double min_consumer_time_sec;           // Fastest consumer
    double max_consumer_time_sec;           // Slowest consumer
    double std_dev_consumer_time_sec;       // Standard deviation of consumer times
    long total_operations;                  // Total operations across all consumers
    int num_consumers;                      // Number of consumers
    
    // Per-process details (optional, allocated if needed)
    micro_process_result_t *process_results; // Array of per-process results
} micro_bench_results_t;

// Micro benchmark context
typedef struct {
    void *queue;                    // Generic queue pointer
    micro_bench_config_t config;
    micro_process_result_t local_result;
    micro_bench_results_t global_results;
    
    struct timeval benchmark_start; // Global benchmark start time
    struct timeval local_start;     // Local process start time
    struct timeval local_end;       // Local process end time
    
    int mpi_rank;
    int mpi_size;
    MPI_Comm mpi_comm;
} micro_bench_ctx_t;


/**
 * Initialize micro benchmark context
 * 
 * @param ctx Context to initialize
 * @param queue Queue pointer (generic void*)
 * @param config Benchmark configuration
 * @param mpi_rank MPI rank of this process
 * @param mpi_size Total MPI processes
 * @param mpi_comm MPI communicator
 * @return 0 on success, -1 on error
 */
int micro_bench_init(micro_bench_ctx_t *ctx, 
                     void *queue,
                     const micro_bench_config_t *config,
                     int mpi_rank,
                     int mpi_size,
                     MPI_Comm mpi_comm);

/**
 * Create default micro benchmark configuration
 * 
 * @param num_consumers Number of consumer processes
 * @param ops_per_consumer Operations per consumer (default: 10000)
 * @return Configuration structure
 */
micro_bench_config_t micro_bench_config_create(int num_consumers, int ops_per_consumer);

/**
 * Phase 1: Warmup - Producer fills queue with (num_consumers * ops_per_consumer) items
 * This ensures consumers will never hit empty queue during measurement
 * 
 * @param ctx Benchmark context
 * @param enqueue_func Function pointer to enqueue operation: int (*)(void *queue, int value)
 * @return Number of items enqueued, -1 on error
 */
int micro_bench_warmup_phase(micro_bench_ctx_t *ctx, 
                              int (*enqueue_func)(void *queue, int value));

/**
 * Phase 2A: Producer measurement phase
 * 
 * Producer enqueues exactly 2 * (num_consumers * ops_per_consumer) items
 * Timing is measured for this phase
 * 
 * @param ctx Benchmark context
 * @param enqueue_func Function pointer to enqueue operation: int (*)(void *queue, int value)
 * @return Number of items enqueued, -1 on error
 */
int micro_bench_producer_phase(micro_bench_ctx_t *ctx,
                                int (*enqueue_func)(void *queue, int value));

/**
 * Phase 2B: Consumer measurement phase
 * 
 * Each consumer dequeues exactly ops_per_consumer items (total N items)
 * Timing is measured for this phase only
 * 
 * @param ctx Benchmark context
 * @param dequeue_func Function pointer to dequeue operation: int (*)(void *queue, int *value)
 *                     Should return 1 on success, 0 if queue empty, -1 on error
 * @return Number of items dequeued, -1 on error
 */
int micro_bench_consumer_phase(micro_bench_ctx_t *ctx,
                                int (*dequeue_func)(void *queue, int *value));

/**
 * Phase 3: Consumer drain phase (not measured)
 * 
 * Consumers dequeue remaining items from queue to clean up
 * This phase is NOT timed - used only to drain the queue
 * 
 * @param ctx Benchmark context
 * @param dequeue_func Function pointer to dequeue operation
 * @param drain_items Total items to drain (typically 2N)
 * @return Number of items dequeued, -1 on error
 */
int micro_bench_consumer_drain(micro_bench_ctx_t *ctx,
                                int (*dequeue_func)(void *queue, int *value),
                                int drain_items);

/**
 * Aggregate results from all consumer processes
 * Calculates total throughput, statistics, and optionally gathers per-process details
 * 
 * @param ctx Benchmark context
 * @param gather_process_details If true, allocate and fill process_results array
 * @return 0 on success, -1 on error
 */
int micro_bench_aggregate_results(micro_bench_ctx_t *ctx, int gather_process_details);

/**
 * Print benchmark results to stdout (rank 0 only)
 * 
 * @param ctx Benchmark context with aggregated results
 */
void micro_bench_print_results(const micro_bench_ctx_t *ctx);

/**
 * Export results to CSV file
 * 
 * @param ctx Benchmark context
 * @param filename Output CSV file path
 * @return 0 on success, -1 on error
 */
int micro_bench_export_csv(const micro_bench_ctx_t *ctx, const char *filename);

/**
 * Export results to CSV file with memory usage
 * 
 * @param ctx Benchmark context
 * @param filename Output CSV file path
 * @param queue_memory_bytes Queue memory usage in bytes
 * @return 0 on success, -1 on error
 */
int micro_bench_export_csv_with_memory(const micro_bench_ctx_t *ctx, const char *filename, size_t queue_memory_bytes);

/**
 * Cleanup and free resources
 * 
 * @param ctx Benchmark context
 */
void micro_bench_cleanup(micro_bench_ctx_t *ctx);


// Helper macros for timing
#define MICRO_BENCH_START_TIMER(ctx) \
    gettimeofday(&(ctx)->local_start, NULL)

#define MICRO_BENCH_STOP_TIMER(ctx) \
    gettimeofday(&(ctx)->local_end, NULL)

#define MICRO_BENCH_CALC_ELAPSED(ctx) \
    ((ctx)->local_end.tv_sec - (ctx)->local_start.tv_sec + \
     ((ctx)->local_end.tv_usec - (ctx)->local_start.tv_usec) / 1000000.0)

#endif // MICRO_BENCHMARK_H
