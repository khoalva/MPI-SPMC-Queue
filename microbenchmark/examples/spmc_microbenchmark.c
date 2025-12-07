#define _GNU_SOURCE
#include "../micro_benchmark.h"
#include "spmc_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mpi.h>

/**
 * SPMC Queue Micro Benchmark Example
 * 
 * This example demonstrates proper micro benchmarking methodology:
 * 1. Warmup: Producer prefills queue with N items (not measured)
 * 2A. Measurement: Producer enqueues 2N items (measured)
 * 2B. Measurement: Consumers dequeue N items (measured, parallel with 2A)
 * 3. Drain: Consumers dequeue remaining 2N items (not measured)
 * 
 * This ensures high contention throughout measurement phase
 * 
 * Usage:
 *   mpirun -np <N> ./spmc_microbenchmark [ops_per_consumer]
 *   
 *   N = total processes (1 producer + consumers)
 *   ops_per_consumer = operations each consumer performs (default: 10000)
 * 
 * Example:
 *   mpirun -np 5 ./spmc_microbenchmark 10000
 *   (1 producer + 4 consumers, each consumer does 10000 dequeues)
 */

// Wrapper functions for queue operations
static int enqueue_wrapper(void *queue, int value) {
    return spmc_queue_enqueue((spmc_queue_t *)queue, value);
}

static int dequeue_wrapper(void *queue, int *value) {
    int out_data[1];  // Buffer for dequeue
    int result = spmc_queue_dequeue((spmc_queue_t *)queue, out_data, 1);
    if (result > 0) {
        *value = out_data[0];
        return 1; // Success
    }
    return 0; // Empty or error
}

static void print_usage(const char *program_name) {
    printf("\nUsage: mpirun -np <N> %s [ops_per_consumer]\n", program_name);
    printf("\nParameters:\n");
    printf("  N                - Total MPI processes (1 producer + consumers)\n");
    printf("  ops_per_consumer - Operations per consumer (default: 10000)\n");
    printf("\nExample:\n");
    printf("  mpirun -np 5 %s 10000\n", program_name);
    printf("  (1 producer + 4 consumers, each consumer does 10000 dequeues)\n\n");
}

int main(int argc, char *argv[]) {
    spmc_queue_t queue;
    int ops_per_consumer = DEFAULT_OPS_PER_CONSUMER;
    
    // Parse command line arguments
    if (argc > 1) {
        if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        ops_per_consumer = atoi(argv[1]);
        if (ops_per_consumer <= 0) {
            fprintf(stderr, "ERROR: ops_per_consumer must be positive\n");
            return 1;
        }
    }
    
    // Initialize SPMC queue
    if (spmc_queue_init(&queue, argc, argv) != 0) {
        fprintf(stderr, "Failed to initialize SPMC queue\n");
        return 1;
    }
    
    // Get MPI info
    int mpi_rank = mpi_get_rank(&queue.mpi_ctx);
    int mpi_size = mpi_get_size(&queue.mpi_ctx);
    
    // Validate: Need at least 2 processes (1 producer + 1 consumer)
    if (mpi_size < 2) {
        if (mpi_rank == 0) {
            fprintf(stderr, "ERROR: Micro benchmark requires at least 2 processes\n");
            fprintf(stderr, "       (1 producer + at least 1 consumer)\n");
            fprintf(stderr, "Usage: mpirun -np <N> %s [ops_per_consumer] (where N >= 2)\n", argv[0]);
        }
        spmc_queue_destroy(&queue);
        return 1;
    }
    
    int num_consumers = mpi_size - 1; // All except rank 0
    
    // Print initial info (only from rank 0)
    if (mpi_rank == 0) {
        printf("\n=== SPMC Queue Micro Benchmark ===\n");
        printf("Configuration:\n");
        printf("  Total processes:      %d\n", mpi_size);
        printf("  Producer (rank 0):    1\n");
        printf("  Consumers (rank 1+):  %d\n", num_consumers);
        printf("  Ops per consumer:     %d\n", ops_per_consumer);
        printf("  Total operations:     %d\n", num_consumers * ops_per_consumer);
        printf("\n");
    }
    
    // Create micro benchmark configuration
    micro_bench_config_t config = micro_bench_config_create(num_consumers, ops_per_consumer);
    config.enable_verbose = 1; // Enable per-process details
    
    // Initialize micro benchmark context
    micro_bench_ctx_t bench_ctx;
    if (micro_bench_init(&bench_ctx, &queue, &config, 
                         mpi_rank, mpi_size, queue.mpi_ctx.comm) != 0) {
        fprintf(stderr, "Rank %d: Failed to initialize micro benchmark context\n", mpi_rank);
        spmc_queue_destroy(&queue);
        return 1;
    }
    
    // ===================================================================
    // Phase 1: WARMUP - Producer prefills queue
    // ===================================================================
    if (mpi_rank == 0) {
        int warmup_result = micro_bench_warmup_phase(&bench_ctx, enqueue_wrapper);
        if (warmup_result < 0) {
            fprintf(stderr, "Rank 0: Warmup phase failed\n");
            micro_bench_cleanup(&bench_ctx);
            spmc_queue_destroy(&queue);
            return 1;
        }
    }
    
    // Synchronize after warmup - ensure all processes see the prefilled queue
    MPI_Barrier(queue.mpi_ctx.comm);
    
    if (mpi_rank == 0) {
        printf("\nAll processes synchronized after warmup\n");
        printf("Starting measurement phase...\n\n");
    }
    
    // ===================================================================
    // Phase 2A & 2B: MEASUREMENT - Producer and consumers work simultaneously
    // ===================================================================
    // Phase 2A: Producer enqueues 2N items (measured)
    // Phase 2B: Consumers dequeue N items (measured)
    
    if (mpi_rank == 0) {
        // Producer process - enqueue 2N items (measured)
        int produce_result = micro_bench_producer_phase(&bench_ctx, enqueue_wrapper);
        if (produce_result < 0) {
            fprintf(stderr, "Rank 0: Producer phase failed\n");
            micro_bench_cleanup(&bench_ctx);
            spmc_queue_destroy(&queue);
            return 1;
        }
    } else {
        // Consumer process - dequeue N items (measured)
        int consume_result = micro_bench_consumer_phase(&bench_ctx, dequeue_wrapper);
        if (consume_result < 0) {
            fprintf(stderr, "Rank %d: Consumer phase failed\n", mpi_rank);
            micro_bench_cleanup(&bench_ctx);
            spmc_queue_destroy(&queue);
            return 1;
        }
    }
    
    // Wait for measurement phase to complete
    MPI_Barrier(queue.mpi_ctx.comm);
    
    // ===================================================================
    // Phase 3: DRAIN - Consumers dequeue remaining 2N items (not measured)
    // ===================================================================
    if (mpi_rank != 0) {
        int drain_total = num_consumers * ops_per_consumer * 2;  // 2N items
        int drain_result = micro_bench_consumer_drain(&bench_ctx, dequeue_wrapper, drain_total);
        if (drain_result < 0) {
            fprintf(stderr, "Rank %d: Consumer drain phase failed\n", mpi_rank);
            // Don't fail the benchmark, just warn
        }
    }
    
    // Wait for drain phase to complete
    MPI_Barrier(queue.mpi_ctx.comm);
    
    if (mpi_rank == 0) {
        printf("\nAll consumers completed\n");
        printf("Aggregating results...\n\n");
    }
    
    // ===================================================================
    // Phase 3: AGGREGATE AND REPORT RESULTS
    // ===================================================================
    if (micro_bench_aggregate_results(&bench_ctx, 1) != 0) {
        fprintf(stderr, "Rank %d: Failed to aggregate results\n", mpi_rank);
        micro_bench_cleanup(&bench_ctx);
        spmc_queue_destroy(&queue);
        return 1;
    }
    
    // Print results (rank 0 only)
    if (mpi_rank == 0) {
        micro_bench_print_results(&bench_ctx);
        
        // Get queue memory usage
        size_t queue_memory_bytes = spmc_queue_get_capacity_bytes(&queue);
        double queue_memory_mb = queue_memory_bytes / (1024.0 * 1024.0);
        printf("Queue Memory Usage: %.2f MB (%zu bytes)\n", queue_memory_mb, queue_memory_bytes);
        
        // Export to CSV with memory info
        char filename[256];
        snprintf(filename, sizeof(filename), 
                 "microbench_spmc_%dprocs_%dops.csv", 
                 mpi_size, ops_per_consumer);
        micro_bench_export_csv_with_memory(&bench_ctx, filename, queue_memory_bytes);
    }
    
    // Cleanup
    micro_bench_cleanup(&bench_ctx);
    spmc_queue_destroy(&queue);
    
    if (mpi_rank == 0) {
        printf("\nMicro benchmark completed successfully!\n\n");
    }
    
    return 0;
}
