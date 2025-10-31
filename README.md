# SPMC Queue with MPI RMA

A high-performance **Single Producer Multiple Consumer (SPMC)** queue implementation using MPI one-sided communication for distributed computing environments.

## 🎯 Overview

This project implements a **wait-free SPMC queue** optimized for distributed-memory systems using MPI Remote Memory Access (RMA) operations. The system provides concurrent access with one producer and multiple consumers.

### ✨ Key Features

- **👑 Single Producer**: One process handles all enqueue operations  
- **🔄 Multiple Consumers**: Multiple processes dequeue concurrently
- **🌐 MPI RMA Based**: Uses one-sided communication for shared-memory emulation
- **📊 Advanced Benchmarking**: Comprehensive performance testing framework
- **🏗️ Clean Architecture**: Modular design with clear separation of concerns

## 🚀 Quick Start

```bash
# 1. Build the SPMC implementation (10 seconds)
cd spmc_2004
make all

# 2. Build benchmark tools (15 seconds)  
cd ../benchmark
make all

# 3. Run quick validation test (5 seconds)
./run_benchmarks.sh

# 4. Run comprehensive benchmark
./run_benchmarks.sh throughput --processes 4 --spmc-path ../spmc_2004
```

## 📁 Project Structure

```
spmc/
├── spmc_2004/              # 🏗️ Core SPMC queue implementation
│   ├── spmc_queue.c/.h     # SPMC algorithm implementation
│   ├── main_spmc.c         # Main program
│   ├── queue_spmc          # Compiled executable
│   └── Makefile            # Build configuration
├── mpi_lib/                # 🔧 MPI wrapper library  
│   ├── src/mpi_lib.c       # MPI utilities and helpers
│   ├── include/mpi_lib.h   # Library interface
│   └── lib/                # Compiled library files
├── benchmark/              # 📊 Performance benchmarking suite
│   ├── benchmark.c/.h      # Benchmark library core
│   ├── examples/           # Benchmark executables
│   ├── results/            # Organized benchmark results
│   ├── logs/               # Execution logs
│   ├── run_benchmarks.sh   # Main benchmark runner
│   └── quick_test.sh       # Fast validation tests
└── DOCUMENTATION.md        # 📚 Complete technical guide
```

## 🏃 Getting Started

### Prerequisites
- **MPI Implementation**: MPICH
- **C Compiler**: GCC or compatible with C99 support
- **Build Tools**: Make
- **Optional**: WSL for Windows users

### Build Process
```bash
# Build everything from scratch
make -C spmc_2004 all          # Build SPMC implementation
make -C benchmark all          # Build benchmark tools

# Clean build
make -C spmc_2004 clean        # Clean SPMC build
make -C benchmark clean        # Clean benchmark build
```

### Running Tests
```bash
# Quick validation (2-3 processes, ~5 seconds)
cd benchmark && ./run_benchmarks.sh

# Custom benchmark runs
./run_benchmarks.sh quick -p 3
```

## � Benchmark Results

The benchmark framework automatically organizes results by:
- **SPMC Implementation Type** (e.g., `spmc_2004`)
- **Test Type** (quick, throughput, latency, etc.)
- **Process Count** (2, 3, 4, 6, 8, etc.)
- **Timestamp** (for result uniqueness)

Example result structure:
```
benchmark/results/
└── spmc_2004_throughput_4procs_20250707_231002/
    └── spmc_2004_throughput_4procs_20250707_231002.csv
```

### CSV Output Format
```csv
test_type,processes,operations,total_time_sec,throughput_ops_per_sec,avg_latency_ms,producer_rank,consumer_ranks
throughput,4,10000,2.543,3932.15,0.254,0,"1,2,3"
```

## �📖 Documentation

- **[DOCUMENTATION.md](DOCUMENTATION.md)** - 📚 **Complete technical guide** (algorithms, implementation details, advanced usage)
- **[spmc_2004/Readme.md](spmc_2004/Readme.md)** - Core SPMC implementation details
- **[mpi_lib/README.md](mpi_lib/README.md)** - MPI wrapper library documentation  
- **[benchmark/README.md](benchmark/README.md)** - Benchmark framework technical details

### Quick Navigation
- 🏃 **Getting Started**: Build and run instructions above
- 📊 **Benchmark Guide**: See DOCUMENTATION.md → Benchmarking
- 🔧 **Algorithm Details**: See DOCUMENTATION.md → Implementation  
- 🏗️ **Architecture**: See DOCUMENTATION.md → System Design
- 🐛 **Troubleshooting**: See DOCUMENTATION.md → Common Issues

