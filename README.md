# MPI-SPMC-Queue

A high-performance Single Producer Multiple Consumer (SPMC) queue implementation using MPI one-sided communication.

## 🎯 Overview

This project implements a wait-free SPMC queue based on Matei David's algorithm, designed for distributed-memory environments using MPI RMA operations.

### Key Features
- **Wait-Free Operations**: Bounded execution time for all operations
- **Single Producer**: One process (rank 0) enqueues items  
- **Multiple Consumers**: Multiple processes can dequeue concurrently
- **MPI RMA**: Uses one-sided communication for shared-memory emulation
- **Comprehensive Benchmarking**: Built-in performance analysis tools

## 🚀 Quick Start

```bash
# Build and test (30 seconds)
cd spmc_2004 && make
cd ../benchmark && make all
./quick_test.sh
```

## 📁 Project Structure

```
spmc/
├── spmc_2004/          # Core SPMC queue implementation
├── mpi_lib/            # MPI wrapper library
├── benchmark/          # Performance benchmarking tools
└── DOCUMENTATION.md    # 📚 Complete guide (read this!)
```

## 📖 Documentation

- **[DOCUMENTATION.md](DOCUMENTATION.md)** - 📚 **Complete comprehensive guide** (all-in-one)
- **[spmc_2004/Readme.md](spmc_2004/Readme.md)** - Core implementation details
- **[mpi_lib/README.md](mpi_lib/README.md)** - MPI wrapper library docs
- **[benchmark/README.md](benchmark/README.md)** - Benchmark library technical details
- **[legacy_docs/](legacy_docs/)** - Old documentation files (consolidated)

### For Quick Reference:
- 🏃 **Getting Started**: See DOCUMENTATION.md → Quick Start
- 📊 **Running Benchmarks**: See DOCUMENTATION.md → Benchmark Guide  
- 🔧 **Adding New SPMC**: See DOCUMENTATION.md → Adding New Implementation
- 🏗️ **Architecture**: See DOCUMENTATION.md → Architecture

## 🆕 What Changed?

✅ **Consolidated Documentation**: All scattered .md files → Single DOCUMENTATION.md  
✅ **Clear Structure**: README.md for overview, DOCUMENTATION.md for details  
✅ **Legacy Preserved**: Old files moved to `legacy_docs/` for reference  
✅ **Better Navigation**: Table of contents and clear sections
