# SPMC Queue with MPI RMA

A high-performance **Single Producer Multiple Consumer (SPMC)** queue implementation using MPI one-sided communication for distributed computing environments.

## 🎯 Overview

This project implements a **wait-free SPMC queue** optimized for distributed-memory systems using MPI Remote Memory Access (RMA) operations. The system provides concurrent access with one producer and multiple consumers.

### ✨ Key Features

- **🚀 Wait-Free Operations**: Non-blocking concurrent access
- **👑 Single Producer**: One process handles all enqueue operations  
- **🔄 Multiple Consumers**: Multiple processes dequeue concurrently
- **🌐 MPI RMA Based**: Uses one-sided communication for shared-memory emulation
- **📊 Advanced Benchmarking**: Comprehensive performance testing framework
- **🐼 Pandas-Ready**: CSV outputs for data analysis
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
./quick_test.sh

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
- **MPI Implementation**: OpenMPI, MPICH, or Intel MPI
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
cd benchmark && ./quick_test.sh

# Custom benchmark runs
./run_benchmarks.sh quick --processes 3
./run_benchmarks.sh throughput --processes 6 --spmc-path ../spmc_2004
./run_benchmarks.sh suite --processes 4 --export-csv
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

### CSV Output Format (Pandas-Ready)
```csv
test_type,processes,operations,total_time_sec,throughput_ops_per_sec,avg_latency_ms,producer_rank,consumer_ranks
throughput,4,10000,2.543,3932.15,0.254,0,"1,2,3"
```

## 🔧 Advanced Usage

### Custom SPMC Implementation Testing
```bash
# Test different SPMC implementations
./run_benchmarks.sh suite --spmc-path /path/to/custom/spmc
./run_benchmarks.sh latency --spmc-path ../spmc_2004 --processes 8
```

### Batch Analysis
```bash
# Run multiple configurations
for procs in 2 4 6 8; do
    ./run_benchmarks.sh throughput --processes $procs
done

# Results ready for pandas analysis
python3 -c "
import pandas as pd
import glob
csvs = glob.glob('results/*/*.csv')
df = pd.concat([pd.read_csv(f) for f in csvs])
print(df.groupby('processes')['throughput_ops_per_sec'].mean())
"
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

## 🚧 Recent Updates

✅ **Streamlined Implementation**: Removed original queue implementation, focus on SPMC only  
✅ **Enhanced Benchmarking**: Single CSV output, organized results, pandas-ready format  
✅ **Improved Build System**: Simplified Makefile, better dependency management  
✅ **Flexible Testing**: Configurable SPMC implementation paths, comprehensive test suite  
✅ **Better Documentation**: Consolidated guides, clear structure, practical examples

## 🤝 Contributing

1. **Fork** the repository
2. **Create** a feature branch (`git checkout -b feature/amazing-feature`)
3. **Commit** your changes (`git commit -m 'Add amazing feature'`)
4. **Push** to the branch (`git push origin feature/amazing-feature`)
5. **Open** a Pull Request

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 🙏 Acknowledgments

- Based on research in wait-free concurrent data structures
- MPI RMA optimization techniques for distributed computing
- Performance benchmarking methodologies for concurrent systems
