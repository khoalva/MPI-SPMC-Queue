# Micro Benchmark Library for SPMC Queue

This library implements **proper micro benchmarking methodology** for evaluating SPMC queue performance in high-contention scenarios.

## Key Differences from System Benchmark

### System Benchmark (`../benchmark/`)
- Measures overall system behavior
- Variable operations (depends on timing)
- Includes coordination overhead
- Good for end-to-end testing

### Micro Benchmark (this library)
- Measures pure queue performance
- **Fixed operations per consumer** (e.g., 10000 dequeues)
- Eliminates empty queue waits
- **High contention guaranteed** throughout measurement
- Reproducible, consistent results

## Benchmark Methodology

### Phase 1: Warmup (not measured)
```
Producer enqueues N items (N = num_consumers × ops_per_consumer)
Purpose: Pre-fill queue to eliminate cold start effects
```

### Phase 2A: Producer Measurement
```
Producer enqueues 2N items (MEASURED)
Purpose: Measure producer throughput under realistic contention
```

### Phase 2B: Consumer Measurement (parallel with 2A)
```
Each consumer dequeues ops_per_consumer items (MEASURED)
Total: N items dequeued by all consumers
Purpose: Measure consumer throughput with guaranteed queue items
```

### Phase 3: Drain (not measured)
```
Consumers dequeue remaining 2N items
Purpose: Clean up queue for next test
```

## Queue State Throughout Benchmark

```
Initial:     EMPTY
After Phase 1:   N items
During Phase 2:  N → 3N → 2N items (always has buffer)
After Phase 3:   EMPTY (cleaned)
```

## Why This Works for SPMC Queues

In SPMC (Single Producer, Multiple Consumers):
- **Consumers aggregate throughput >> Producer throughput**
- Example: 1 producer @ 100K ops/sec vs 4 consumers @ 320K ops/sec total
- Phase 2A (2N items) ensures producer enqueues enough to keep queue full
- Phase 1 warmup (N items) provides buffer to prevent queue empty
- Phase 3 drain ensures accurate measurement without queue starvation

## Usage

### Build
```bash
make
```

### Run Example
```bash
cd examples
mpirun -np 5 ./spmc_microbenchmark 10000
# (1 producer + 4 consumers, 10000 ops per consumer)
```

### Run Test Suite
```bash
make test               # Standard test
make test-micro-small   # Smaller workload
make test-micro-large   # Larger workload
```

### Run Full Benchmark Suite
```bash
./run_microbenchmarks.sh
# Runs multiple queue implementations with various consumer counts
```

## Configuration

Default: 10000 operations per consumer

Customize via command line:
```bash
mpirun -np <N> ./spmc_microbenchmark <ops_per_consumer>
```

## Output

### Console Output
```
=== MICRO BENCHMARK RESULTS ===
Configuration:
  Number of consumers:  4
  Ops per consumer:     10000
  Total operations:     40000

Throughput:
  Total time:           0.523000 seconds
  Total throughput:     76,481.35 ops/sec
  Total throughput:     0.08 Mops/sec

Consumer Statistics:
  Average time:         0.520000 seconds
  Min time:             0.518000 seconds
  Max time:             0.523000 seconds
  Std deviation:        0.002000 seconds
  Time variation:       0.38%

Per-Process Details:
  Rank   Role      Operations   Time (sec)      Throughput       Start (sec)     Finish (sec)
  0      Producer  80000        0.515000        155326.09        0.100000        0.615000
  1      Consumer  10000        0.518000        19305.02         0.100000        0.618000
  2      Consumer  10000        0.520000        19230.77         0.100000        0.620000
  3      Consumer  10000        0.521000        19194.26         0.100000        0.621000
  4      Consumer  10000        0.523000        19120.46         0.100000        0.623000
```

### CSV Export
Results are automatically exported to:
```
microbench_spmc_<N>procs_<ops>ops.csv
```

## Integration

### Use with Your Queue

```c
#include "micro_benchmark.h"

// Define wrapper functions
int my_enqueue(void *queue, int value) {
    return my_queue_enqueue((my_queue_t*)queue, value);
}

int my_dequeue(void *queue, int *value) {
    int result = my_queue_dequeue((my_queue_t*)queue);
    if (result >= 0) {
        *value = result;
        return 1; // Success
    }
    return 0; // Empty
}

// Run benchmark
micro_bench_config_t config = micro_bench_config_create(num_consumers, 10000);
micro_bench_ctx_t ctx;
micro_bench_init(&ctx, &my_queue, &config, mpi_rank, mpi_size, MPI_COMM_WORLD);

// Phase 1: Warmup
micro_bench_warmup_phase(&ctx, my_enqueue);
MPI_Barrier(MPI_COMM_WORLD);

// Phase 2A & 2B: Measurement
if (mpi_rank == 0) {
    micro_bench_producer_phase(&ctx, my_enqueue);
} else {
    micro_bench_consumer_phase(&ctx, my_dequeue);
}
MPI_Barrier(MPI_COMM_WORLD);

// Phase 3: Drain
if (mpi_rank != 0) {
    micro_bench_consumer_drain(&ctx, my_dequeue, drain_items);
}

// Aggregate and print results
micro_bench_aggregate_results(&ctx, 1);
micro_bench_print_results(&ctx);
micro_bench_cleanup(&ctx);
```

## API Reference

See `micro_benchmark.h` for complete API documentation.

Key functions:
- `micro_bench_init()` - Initialize benchmark context
- `micro_bench_warmup_phase()` - Warmup phase (producer only)
- `micro_bench_producer_phase()` - Producer measurement
- `micro_bench_consumer_phase()` - Consumer measurement
- `micro_bench_consumer_drain()` - Drain phase (consumers only)
- `micro_bench_aggregate_results()` - Collect results from all processes
- `micro_bench_print_results()` - Display results (rank 0 only)
- `micro_bench_export_csv()` - Export to CSV file
- `micro_bench_cleanup()` - Free resources

## License

Part of MPI-SPMC-Queue project.
