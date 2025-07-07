# SPMC Queue Benchmark Library

A comprehensive performance benchmarking library for Single Producer Multiple Consumer (SPMC) queue implementations using MPI.

## Features

### 🚀 **Performance Metrics**
- **Throughput**: Items processed per second
- **Latency**: Enqueue/dequeue operation timing
- **Load Balancing**: Distribution efficiency across consumers
- **Memory Usage**: Peak memory consumption tracking
- **Scalability**: Performance across different process counts

### 📊 **Benchmark Types**
- **Quick Test**: Fast validation with 1,000 items
- **Throughput Test**: High-volume processing with 10,000 items  
- **Latency Test**: Detailed timing analysis with microsecond precision
- **Scalability Test**: Performance scaling from 2-8 processes
- **Stress Test**: Extended duration testing under high load

### 📈 **Advanced Analytics**
- Statistical analysis of latency distributions
- Load balance scoring (0-100 scale)
- CSV export for data analysis
- Real-time progress monitoring
- Memory profiling capabilities

## Quick Start

### Building the Library

```bash
# Build everything (library + examples)
make all

# Build specific targets
make static    # Static library only
make shared    # Shared library only
make examples  # Example programs only
```

### Running Benchmarks

```bash
# Quick validation test
make test-quick

# Comprehensive benchmark suite
make test-all

# Individual test types
make test-throughput
make test-latency
```

### Manual Execution

```bash
# Basic throughput test with 4 processes
mpirun -np 4 ./examples/spmc_benchmark throughput

# Latency analysis with 3 processes
mpirun -np 3 ./examples/spmc_benchmark latency

# Scalability test with 6 processes
mpirun -np 6 ./examples/spmc_benchmark scalability
```

## API Usage

### Basic Benchmark Setup

```c
#include "benchmark.h"

// Initialize benchmark context
benchmark_ctx_t ctx;
benchmark_config_t config = benchmark_config_throughput_test(10000);

int is_producer = spmc_queue_is_enqueuer(queue);
int mpi_rank = mpi_get_rank(&queue->mpi_ctx);
int mpi_size = mpi_get_size(&queue->mpi_ctx);

benchmark_init(&ctx, queue, &config, is_producer, mpi_rank, mpi_size);
```

### Recording Operations

```c
// Start timing
benchmark_start(&ctx);

// Producer: Record enqueue operations
BENCHMARK_RECORD_ENQUEUE(&ctx, spmc_queue_enqueue(queue, value));

// Consumer: Record dequeue operations  
BENCHMARK_RECORD_DEQUEUE(&ctx, value = spmc_queue_dequeue(queue));

// Stop timing and calculate results
benchmark_stop(&ctx);
```

### Results and Reporting

```c
// Aggregate results from all MPI processes
benchmark_aggregate_results(&ctx, &queue->mpi_ctx.comm);

// Print comprehensive report
benchmark_print_report(&ctx);

// Export to CSV for analysis
benchmark_export_csv(&ctx, "results.csv");

// Cleanup resources
benchmark_cleanup(&ctx);
```

## Configuration Options

### Predefined Configurations

```c
// Quick validation (1,000 items)
benchmark_config_t config = benchmark_config_throughput_test(1000);

// Detailed latency analysis  
benchmark_config_t config = benchmark_config_latency_test(5000);

// Scalability testing
benchmark_config_t config = benchmark_config_scalability_test(20000, num_processes);

// Stress testing (60 seconds)
benchmark_config_t config = benchmark_config_stress_test(60);
```

### Custom Configuration

```c
benchmark_config_t config = {
    .num_items = 15000,             // Items to process
    .num_producers = 1,             // Producer count  
    .num_consumers = 4,             // Consumer count
    .producer_delay_us = 1000,      // Producer delay (μs)
    .consumer_delay_us = 2000,      // Consumer delay (μs)
    .warmup_items = 500,            // Warmup phase
    .test_duration_sec = 120,       // Max duration
    .enable_latency_tracking = 1,   // Detailed timing
    .enable_memory_tracking = 1,    // Memory profiling
    .test_name = "Custom Test"      // Test identifier
};
```

## Performance Metrics

### Throughput Analysis
- **Items/Second**: Total processing rate
- **Producer Rate**: Enqueue operations per second
- **Consumer Rate**: Dequeue operations per second
- **System Efficiency**: Overall utilization

### Latency Measurements
- **Average Latency**: Mean operation time
- **Maximum Latency**: Worst-case timing
- **Percentile Analysis**: 50th, 95th, 99th percentiles
- **Jitter Analysis**: Timing consistency

### Load Balancing
- **Distribution Score**: 0-100 effectiveness rating
- **Consumer Utilization**: Work distribution analysis
- **Variance Analysis**: Load balancing consistency

## Sample Output

```
================================================================
BENCHMARK RESULTS: Throughput Test
================================================================
Performance Metrics:
  Total execution time:     2.456 seconds
  Total items produced:     10000
  Total items consumed:     10000
  Throughput:              4071.66 items/second
  Load balance score:       94/100

Enqueue Latency:
  Average:                 125.34 μs
  Maximum:                 1205.67 μs

Dequeue Latency:
  Average:                 89.23 μs
  Maximum:                 892.45 μs

Memory Usage:
  Peak memory:             2048 KB
================================================================
```

## File Structure

```
benchmark/
├── benchmark.h              # Public API header
├── benchmark.c              # Core implementation
├── Makefile                 # Build configuration
├── README.md               # This documentation
├── lib/                    # Generated libraries
│   ├── libbenchmark.a      # Static library
│   └── libbenchmark.so*    # Shared library
├── obj/                    # Object files
└── examples/
    ├── spmc_benchmark.c    # SPMC queue benchmark
    └── benchmark_*.csv     # Generated results
```

## Dependencies

- **MPI**: Message Passing Interface
- **SPMC Queue**: Target queue implementation
- **Standard C Libraries**: math.h, sys/time.h, sys/resource.h

## Build Requirements

- MPI compiler (mpicc)
- C99 standard support
- POSIX-compliant system
- Linux/WSL environment

## Integration

To integrate with your SPMC queue project:

1. Include the benchmark library in your build system
2. Link against `-lbenchmark -lm`
3. Include the header: `#include "benchmark.h"`
4. Follow the API usage patterns above

## Performance Tips

- **Warmup Phase**: Use warmup iterations for stable measurements
- **Process Count**: Test with 2-8 processes for scalability analysis
- **Timing Precision**: Enable latency tracking for detailed analysis
- **Memory Profiling**: Monitor memory usage in long-running tests
- **CSV Export**: Use exported data for statistical analysis

## License

This benchmark library is part of the SPMC Queue project and follows the same licensing terms.
