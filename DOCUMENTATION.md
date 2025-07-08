# 📚 SPMC Queue - Technical Documentation

Complete guide for building, running, and benchmarking the SPMC queue implementation.

## 📋 Table of Contents
- [🎯 System Overview](#-system-overview)
- [🚀 Quick Start Guide](#-quick-start-guide)  
- [🏗️ Architecture](#️-architecture)
- [📊 Benchmarking Framework](#-benchmarking-framework)
- [🔧 Configuration](#-configuration)
- [️ Troubleshooting](#️-troubleshooting)

---

## 🎯 System Overview

### Technologies Used

- **MPI (Message Passing Interface)**: For distributed computing and RMA operations
- **C99**: Core implementation language
- **GNU Make**: Build system
- **Bash**: Shell scripts for automation
- **CSV**: Data export format for analysis

### System Requirements

- **MPI Implementation**: OpenMPI 4.0+, MPICH 3.3+, or Intel MPI
- **C Compiler**: GCC 7+ or Clang 8+ with C99 support
- **Platform**: Linux, macOS, or WSL on Windows
- **Memory**: 1-4 GB RAM recommended
- **Processes**: 2-16 MPI processes

---

## 🚀 Quick Start Guide

### Prerequisites Installation

#### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install mpich mpich-dev build-essential
```

#### CentOS/RHEL
```bash
sudo yum install mpich mpich-devel gcc make
```

#### macOS
```bash
brew install mpich
```

### Build & Run

```bash
# 1. Build SPMC implementation
cd spmc_2004
make all                    # Creates queue_spmc executable

# 2. Build benchmark framework  
cd ../benchmark
make all                    # Creates benchmark library & tools

# 3. Quick validation test
./quick_test.sh             # Tests with 3 processes

# 4. Run performance benchmark
./run_benchmarks.sh throughput --processes 4
```

---

## 🏗️ Architecture

### System Components

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   SPMC Queue    │    │   MPI Library   │    │   Benchmark     │
│   (spmc_2004/)  │◄──►│   (mpi_lib/)    │◄──►│  (benchmark/)   │
│                 │    │                 │    │                 │
│ • spmc_queue.c  │    │ • mpi_lib.c     │    │ • benchmark.c   │
│ • main_spmc.c   │    │ • Window mgmt   │    │ • CSV export    │
│ • queue_spmc    │    │ • Error handling│    │ • Test runners  │
└─────────────────┘    └─────────────────┘    └─────────────────┘
```

### Process Roles

- **Producer (Rank 0)**: Handles all enqueue operations
- **Consumers (Rank 1,2,3,...)**: Handle dequeue operations concurrently
- **Coordination**: MPI RMA operations for shared memory access

### Technology Stack

1. **MPI RMA (Remote Memory Access)**
   - `MPI_Win_create()`: Shared memory windows
   - `MPI_Compare_and_swap()`: Atomic operations
   - `MPI_Fetch_and_op()`: Atomic increments
   - `MPI_Win_flush()`: Memory synchronization

2. **Build System**
   - `Makefile` with automatic dependency detection
   - Static and shared library generation
   - Cross-platform compatibility

3. **Data Export**
   - CSV format for pandas/R analysis
   - Timestamp-based result organization
   - Comprehensive metrics collection

---

## 📊 Benchmarking Framework

### Available Test Types

#### Quick Commands
```bash
# Fast validation (1-2 minutes)
./quick_test.sh                    # 3 processes
./quick_test.sh 4                  # 4 processes

# Performance benchmarks
./run_benchmarks.sh quick --processes 3       # Quick test
./run_benchmarks.sh throughput --processes 4  # Max performance
./run_benchmarks.sh latency --processes 6     # Timing analysis  
./run_benchmarks.sh suite --processes 4       # Complete tests
```

#### Test Parameters
| Test Type | Duration | Items | Purpose |
|-----------|----------|-------|---------|
| `quick` | 10-30 sec | 1,000 | Development validation |
| `throughput` | 1-3 min | 10,000+ | Maximum performance |
| `latency` | 30-120 sec | 5,000 | Timing analysis |
| `suite` | 3-10 min | Multiple | Complete profile |

### Command Line Options

```bash
./run_benchmarks.sh <TEST_TYPE> [OPTIONS]

Options:
  -p, --processes NUM     # Number of MPI processes (default: 3)
  -s, --spmc-path PATH    # Path to SPMC implementation  
  -o, --output DIR        # Output directory for results
  -v, --verbose           # Enable verbose output
  --no-build              # Skip build step
  --export-csv            # Export results to CSV (default: true)
```

### Result Organization

Results are automatically organized by timestamp and configuration:

```
benchmark/results/
├── spmc_2004_quick_3procs_20250707_231002/
│   └── spmc_2004_quick_3procs_20250707_231002.csv
├── spmc_2004_throughput_4procs_20250707_231845/  
│   └── spmc_2004_throughput_4procs_20250707_231845.csv
└── spmc_2004_suite_6procs_20250707_232130/
    └── spmc_2004_suite_6procs_20250707_232130.csv
```

### CSV Output (Pandas-Ready)

```csv
test_type,processes,operations,total_time_sec,throughput_ops_per_sec,avg_latency_ms,producer_rank,consumer_ranks
quick,3,1000,0.524,1908.40,0.262,0,"1,2"
throughput,4,10000,2.543,3932.15,0.254,0,"1,2,3"
```

**Key Metrics:**
- `throughput_ops_per_sec`: Operations per second (higher = better)
- `avg_latency_ms`: Average latency in milliseconds (lower = better)
- `total_time_sec`: Total test execution time
- `operations`: Number of items processed

---

## 🔧 Configuration

### Build Configuration

#### SPMC Implementation (`spmc_2004/Makefile`)
```makefile
CC=mpicc                    # MPI compiler
CFLAGS=-O2 -Wall           # Optimization flags
EXECUTABLE=queue_spmc       # Output executable
```

#### Benchmark Framework (`benchmark/Makefile`)
```makefile
# Build targets
make all                   # Complete build
make static               # Static library only
make shared              # Shared library only  
make examples            # Benchmark executables
make clean               # Clean build artifacts
```

### Runtime Configuration

#### Environment Variables
```bash
export OMPI_ALLOW_RUN_AS_ROOT=1    # Allow root execution
export MPI_NUM_CORES=4             # Set core count
export OMP_NUM_THREADS=1           # Disable OpenMP threading
```

#### MPI Runtime Options
```bash
# Basic execution
mpirun -np 4 ./queue_spmc

# With binding and output control
mpirun --bind-to core --report-bindings -np 4 ./queue_spmc

# Debugging mode
mpirun --mca btl_base_verbose 30 -np 4 ./queue_spmc
```

### Testing Different Implementations

#### Directory Structure for Multiple SPMC Versions
```
spmc/
├── spmc_2004/             # Current implementation
├── spmc_new/             # New implementation 
└── benchmark/            # Shared benchmark framework
```

#### Testing New Implementations
```bash
# Test different implementations
./run_benchmarks.sh throughput --spmc-path ../spmc_2004
./run_benchmarks.sh throughput --spmc-path ../spmc_new

# Compare results
python3 -c "
import pandas as pd
df1 = pd.read_csv('results/spmc_2004_*/**.csv')
df2 = pd.read_csv('results/spmc_new_*/**.csv') 
print('2004 throughput:', df1['throughput_ops_per_sec'].mean())
print('New throughput:', df2['throughput_ops_per_sec'].mean())
"
```

---

## 🛠️ Troubleshooting

### Common Build Issues

#### 1. MPI Not Found
```bash
# Ubuntu/Debian
sudo apt-get install mpich mpich-dev

# CentOS/RHEL  
sudo yum install mpich mpich-devel

# Verify installation
which mpicc
mpicc --version
```

#### 2. Compilation Errors
```bash
# Clean and rebuild
make clean && make all

# Check compiler flags
echo $CFLAGS

# Verbose build
make VERBOSE=1
```

### Runtime Issues

#### 1. Permission Denied
```bash
# Allow root execution (if needed)
mpirun --allow-run-as-root -np 3 ./queue_spmc

# WSL specific
wsl -e bash -c "cd /path && mpirun -np 3 ./queue_spmc"
```

#### 2. Process Binding Issues
```bash
# Disable binding
mpirun --bind-to none -np 4 ./queue_spmc

# Use specific hosts
mpirun --host localhost:4 -np 4 ./queue_spmc
```

#### 3. Memory Issues
```bash
# Increase stack size
ulimit -s unlimited

# Check available memory
free -h

# Reduce test size
./quick_test.sh 2    # Use fewer processes
```

### Performance Issues

#### 1. Low Throughput
- **Check CPU binding**: Use `--bind-to core`
- **Verify MPI version**: Ensure recent OpenMPI/MPICH
- **Monitor system load**: Use `htop` or `top`
- **Start small**: Begin with 2-3 processes

#### 2. High Latency
- **Network latency**: Test locally first
- **System noise**: Close unnecessary applications
- **MPI transport**: Try different MPI transports (`--mca btl tcp,self`)

#### 3. Memory Leaks
- **Use valgrind**: `mpirun -np 2 valgrind ./queue_spmc`
- **Check logs**: Look in `benchmark/logs/` for errors
- **Monitor memory**: `watch free -h` during tests

### Getting Help

1. **Check logs**: `benchmark/logs/` contains detailed execution logs
2. **Verify environment**: Ensure MPI and compilers are correctly installed
3. **Start simple**: Use `./quick_test.sh 2` for minimal testing
4. **Build step-by-step**: Build components individually to isolate issues

### Expected Performance Ranges

| System Type | Processes | Expected Throughput | Expected Latency |
|-------------|-----------|-------------------|------------------|
| Laptop | 2-4 | 1,000-5,000 ops/sec | 0.1-1.0 ms |
| Workstation | 4-8 | 5,000-15,000 ops/sec | 0.05-0.5 ms |
| Cluster | 8-16 | 10,000-50,000 ops/sec | 0.01-0.1 ms |

---

*For additional support, refer to the source code comments and MPI documentation.*
