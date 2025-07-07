# 📚 MPI-SPMC-Queue - Complete Documentation

## 📋 Table of Contents
- [Project Overview](#-project-overview)
- [Quick Start](#-quick-start)
- [Benchmark Guide](#-benchmark-guide)
- [Architecture](#-architecture)
- [Adding New SPMC Implementation](#-adding-new-spmc-implementation)
- [MPI Library](#-mpi-library)
- [Troubleshooting](#-troubleshooting)

---

## 🎯 Project Overview

This project implements a wait-free SPMC queue based on Matei David's algorithm, designed for distributed-memory environments using MPI RMA operations. The implementation provides linearizable operations with guaranteed progress.

### Key Features
- **Wait-Free Operations**: Bounded execution time for all operations
- **Single Producer**: One process (rank 0) enqueues items  
- **Multiple Consumers**: Multiple processes can dequeue concurrently
- **MPI RMA**: Uses one-sided communication for shared-memory emulation
- **Comprehensive Benchmarking**: Built-in performance analysis tools

### Project Structure
```
spmc/
├── spmc_2004/          # Core SPMC queue implementation
├── mpi_lib/            # MPI wrapper library
├── benchmark/          # Performance benchmarking tools
└── DOCUMENTATION.md    # This comprehensive guide
```

---

## 🚀 Quick Start

### Prerequisites
- MPI implementation (MPICH, OpenMPI, etc.)
- C compiler with MPI support (mpicc)
- Linux/Unix environment

### Basic Setup
```bash
# Build everything
cd spmc_2004 && make
cd ../benchmark && make all

# Quick test (recommended first step)
./quick_test.sh          # 3 processes, ~10-30 seconds
```

### Quick Commands (Copy & Paste)
```bash
# Different process counts
./quick_test.sh 2        # 2 processes
./quick_test.sh 4        # 4 processes  
./quick_test.sh 6        # 6 processes

# Different test types
./run_benchmarks.sh quick           # Quick test
./run_benchmarks.sh throughput      # Throughput analysis
./run_benchmarks.sh latency         # Latency analysis
./run_benchmarks.sh suite           # Complete test suite
```

---

## 📊 Benchmark Guide

### Features

#### 🚀 Performance Metrics
- **Throughput**: Items processed per second
- **Latency**: Enqueue/dequeue operation timing
- **Load Balancing**: Distribution efficiency across consumers
- **Memory Usage**: Peak memory consumption tracking
- **Scalability**: Performance across different process counts

#### 📈 Benchmark Types
- **Quick Test**: Fast validation with 1,000 items
- **Throughput Test**: High-volume processing with 10,000 items  
- **Latency Test**: Detailed timing analysis with microsecond precision
- **Scalability Test**: Performance scaling from 2-8 processes
- **Stress Test**: Extended duration testing under high load

### 🆕 New Feature: Auto SPMC Type Detection

The benchmark system now automatically detects available SPMC implementations and organizes results by type:

```bash
benchmark/results/
├── spmc_2004/          # Results for SPMC 2004
├── spmc_2005/          # Results for SPMC 2005 (if exists)
└── custom_spmc/        # Results for custom implementations
```

### Result Analysis Tools
```bash
./analyze_results.sh list        # List all results
./analyze_results.sh summary     # Result summary
./analyze_results.sh compare     # Compare SPMC types
./analyze_results.sh details spmc_2004  # Details for specific type
./analyze_results.sh clean       # Cleanup old files
```

### Building the Benchmark Library
```bash
cd benchmark

# Build all components
make all

# Or build individually
make static    # Static library
make shared    # Shared library
make examples  # Benchmark programs
```

### Detailed Test Types

#### 1. Quick Test (Recommended Start)
- **Purpose**: Fast validation and smoke testing
- **Items**: 1,000 items
- **Duration**: ~10-30 seconds
- **Use Case**: Development and CI testing

```bash
./quick_test.sh          # Default 3 processes
./quick_test.sh 4        # 4 processes
```

#### 2. Throughput Test
- **Purpose**: Measure maximum processing speed
- **Items**: 10,000+ items
- **Duration**: 1-5 minutes
- **Metrics**: Items/second, total throughput

```bash
./run_benchmarks.sh throughput -p 4 -i 10000
```

#### 3. Latency Test
- **Purpose**: Detailed timing analysis
- **Focus**: Individual operation timing
- **Precision**: Microsecond level
- **Output**: Latency distribution, percentiles

```bash
./run_benchmarks.sh latency -p 3 -i 5000
```

#### 4. Scalability Test
- **Purpose**: Performance across different process counts
- **Range**: 2-8 processes
- **Metrics**: Scaling efficiency, bottleneck identification

```bash
./run_benchmarks.sh scalability
```

#### 5. Stress Test
- **Purpose**: Extended duration under load
- **Duration**: 10+ minutes
- **Focus**: Memory leaks, stability

```bash
./run_benchmarks.sh stress -i 100000
```

### Understanding Results

#### CSV Output Format
```csv
processes,items,total_time_sec,throughput_items_per_sec,avg_enqueue_latency_us,avg_dequeue_latency_us,total_items_produced,total_items_consumed,load_balance_score
3,1000,0.524,1908.40,45.23,32.67,1000,1000,95.2
```

#### Key Metrics Explained
- **Throughput**: Higher is better (items/second)
- **Latency**: Lower is better (microseconds)
- **Load Balance Score**: 0-100, higher means better distribution
- **Memory Usage**: Peak memory consumption

---

## 🏗️ Architecture

### System Flow
```
[spmc_2004/] → [benchmark/] → [results/]
     ↓              ↓             ↓
 SPMC Queue  →  Benchmark   →   Metrics
 Implementation   Library       & CSV
```

### Core Components

#### 1. SPMC Queue Implementation (`spmc_2004/`)
```c
// spmc_queue.h - Main interface
typedef struct {
    mpi_context_t mpi_ctx;    // MPI context 
    int *head;                // Head pointer
    int *items;               // Queue items
    int row;                  // Current row
    // ... other fields
} spmc_queue_t;

// Core functions:
int spmc_queue_enqueue(spmc_queue_t *queue, int value);  // Producer adds item
int spmc_queue_dequeue(spmc_queue_t *queue);             // Consumer gets item
int spmc_queue_is_enqueuer(spmc_queue_t *queue);         // Check producer/consumer
```

#### 2. Benchmark Library (`benchmark/`)
```c
// benchmark.h - Performance measurement wrapper
typedef struct {
    double total_time_sec;           // Total time
    double throughput_items_per_sec; // Items/second
    double avg_enqueue_latency_us;   // Enqueue latency
    long total_items_produced;       // Total items produced
    // ... other metrics
} benchmark_results_t;

// Timing macros:
BENCHMARK_RECORD_ENQUEUE(&ctx, spmc_queue_enqueue(queue, value));
BENCHMARK_RECORD_DEQUEUE(&ctx, value = spmc_queue_dequeue(queue));
```

#### 3. Test Runner (`benchmark/examples/spmc_benchmark.c`)
Connects SPMC Queue with Benchmark Library, handles:
- MPI initialization and cleanup
- Test type selection
- Result formatting and output
- Error handling

### Algorithm Details

The SPMC queue uses Matei David's wait-free algorithm:

1. **Enqueue Operation** (Producer only):
   - Atomically increment head pointer
   - Write item to calculated position
   - Maximum 3 steps, wait-free

2. **Dequeue Operation** (Multiple consumers):
   - Read current head value
   - Calculate item position
   - Atomically claim item if available
   - Maximum 3 steps, wait-free

3. **Linearizability**: Operations appear atomic and in valid sequential order

---

## 🔧 Adding New SPMC Implementation

### When Creating New SPMC Folder (e.g., `spmc_2025/`)

#### Step 1: Prepare SPMC Implementation

Ensure your new SPMC folder has a compatible interface:

```c
// spmc_new/spmc_queue.h
typedef struct {
    mpi_context_t mpi_ctx;    // Must have MPI context
    // ... other fields
} spmc_queue_t;

// Required functions:
int spmc_queue_init(spmc_queue_t *queue, int argc, char *argv[]);
int spmc_queue_enqueue(spmc_queue_t *queue, int value);
int spmc_queue_dequeue(spmc_queue_t *queue);
int spmc_queue_is_enqueuer(spmc_queue_t *queue);
void spmc_queue_cleanup(spmc_queue_t *queue);
```

#### Step 2: Add Target to benchmark/Makefile

```makefile
# Add to benchmark/Makefile

# Target for new SPMC implementation
spmc_new_benchmark: $(EXAMPLEDIR)/spmc_benchmark.c $(STATIC_LIB)
	$(CC) $(CFLAGS) -I$(INCDIR) -I../spmc_new -I../mpi_lib/include \
		$(EXAMPLEDIR)/spmc_benchmark.c ../spmc_new/spmc_queue.o \
		$(LIBDIR)/$(STATIC_LIB) ../mpi_lib/lib/libmpi_wrapper.a \
		-lm -o $(EXAMPLEDIR)/spmc_new_benchmark

# Test targets
test-new-quick: spmc_new_benchmark
	@echo "Running quick benchmark test for SPMC NEW..."
	cd $(EXAMPLEDIR) && mpirun --allow-run-as-root -np 3 ./spmc_new_benchmark quick

test-new-throughput: spmc_new_benchmark
	@echo "Running throughput benchmark test for SPMC NEW..."
	cd $(EXAMPLEDIR) && mpirun --allow-run-as-root -np 4 ./spmc_new_benchmark throughput
```

#### Step 3: Update Benchmark Scripts

The benchmark system will automatically detect your new implementation if it follows the naming convention `spmc_*`.

#### Step 4: Testing

```bash
# Build new implementation
make spmc_new_benchmark

# Test it
make test-new-quick
make test-new-throughput

# Compare with existing
./analyze_results.sh compare
```

---

## 🔌 MPI Library

### Overview
The project includes a robust MPI wrapper library (`mpi_lib/`) that simplifies MPI programming:

### Features
- **Simplified API**: Reduced parameter counts with sensible defaults
- **Robust Error Handling**: Automatic error detection and reporting
- **Memory Safety**: Built-in validation and bounds checking
- **Window Management**: Easy-to-use one-sided communication helpers
- **Unified Interface**: Single, consistent API

### Core Functions
```c
// Context management
int mpi_init_context(mpi_context_t *ctx, int argc, char *argv[]);
void mpi_cleanup_context(mpi_context_t *ctx);

// Window operations
int mpi_create_window(void *base, size_t size, MPI_Win *win, MPI_Comm comm);
int mpi_put_data(const void *data, size_t count, MPI_Datatype datatype, 
                 int target_rank, size_t target_offset, MPI_Win win);

// Point-to-point communication
int mpi_send_data(const void *data, size_t count, MPI_Datatype datatype, 
                  int dest, int tag, MPI_Comm comm);
int mpi_recv_data(void *data, size_t count, MPI_Datatype datatype, 
                  int source, int tag, MPI_Comm comm);
```

### Error Handling
All functions use the `MPI_TRY` macro for automatic error checking:
```c
MPI_TRY(mpi_send_data(buffer, count, MPI_INT, dest, tag, MPI_COMM_WORLD));
```

---

## 🛠️ Troubleshooting

### Common Issues

#### 1. Build Errors
```bash
# Missing MPI
sudo apt-get install mpich mpich-dev

# Clean build
make clean && make all
```

#### 2. Runtime Errors
```bash
# Permission issues
mpirun --allow-run-as-root -np 3 ./program

# Memory issues
ulimit -s unlimited
```

#### 3. Performance Issues
```bash
# Check system resources
top
free -h

# Reduce test size
./quick_test.sh 2  # Use fewer processes
```

#### 4. Result Analysis
```bash
# Check if results exist
ls -la benchmark/results/

# Clean corrupted results
./analyze_results.sh clean
```

### Getting Help

1. **Check logs**: Look in `benchmark/logs/` for detailed error messages
2. **Validate environment**: Ensure MPI is properly installed
3. **Start small**: Use `quick_test.sh` with 2 processes first
4. **Check dependencies**: Verify all libraries are built correctly

---

## 📈 Performance Tips

### Optimization Guidelines

1. **Process Count**: Start with 2-4 processes for testing
2. **Item Count**: Begin with 1,000 items, scale up gradually
3. **System Resources**: Ensure sufficient memory and CPU
4. **Network**: Use local testing first, then distributed

### Expected Performance

Typical performance on modern systems:
- **Throughput**: 1,000-10,000 items/second
- **Latency**: 10-100 microseconds per operation
- **Scalability**: Linear up to 4-8 processes
- **Memory**: ~1-10 MB depending on queue size

---

*This documentation consolidates all previous guides into a single comprehensive reference. For specific details, refer to the source code and inline comments.*
