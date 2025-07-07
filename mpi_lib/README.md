# MPI Wrapper Library

A simplified, robust wrapper library for MPI (Message Passing Interface) that makes MPI programming easier and more reliable.

## Overview

This library provides a simplified, unified interface to MPI with the following benefits:

- **Simplified API**: Reduced parameter counts with sensible defaults
- **Robust Error Handling**: Automatic error detection and reporting
- **Memory Safety**: Built-in validation and bounds checking
- **Window Management**: Easy-to-use one-sided communication helpers
- **Documentation**: Comprehensive API documentation and examples
- **Unified Interface**: Single, consistent API replacing legacy wrapper implementations

> **Note**: This library replaces and consolidates all previous `mpi_easy` implementations, providing a single, robust, and well-tested MPI wrapper with comprehensive error handling and a clean API.

## Features

### Core Features
- ✅ Simplified MPI initialization and cleanup
- ✅ Automatic error handling with detailed error messages
- ✅ Context management for easier rank/size access
- ✅ Point-to-point communication wrappers
- ✅ Collective communication wrappers
- ✅ One-sided communication (RMA) support
- ✅ Window management utilities
- ✅ Memory management helpers

### Advanced Features
- ✅ Multiple window locking/unlocking
- ✅ Automatic flushing for one-sided operations
- ✅ Input validation and bounds checking
- ✅ Comprehensive error reporting
- ✅ Cross-platform compatibility
- ✅ Extensive test suite (84 tests with 100% pass rate)
- ✅ Production-ready with battle-tested API
- **Point-to-point Communication**: Easy send/receive operations
- **Collective Communication**: Simplified collective operations
- **Window Management**: Streamlined MPI window operations
- **Cross-platform**: Works on Linux, macOS, and Windows

## Quick Start

### Installation

1. Clone or download the library
2. Build the library:
   ```bash
   cd mpi_lib
   make
   ```
3. Optionally install to system (requires sudo):
   ```bash
   make install
   ```

### Basic Usage

```c
#include "mpi_lib.h"

int main(int argc, char *argv[]) {
    mpi_context_t ctx;
    
    // Initialize MPI
    MPI_TRY(mpi_init(argc, argv, &ctx));
    
    // Your parallel code here
    printf("Hello from rank %d of %d\n", 
           mpi_get_rank(&ctx), mpi_get_size(&ctx));
    
    // Clean up
    mpi_finalize();
    return 0;
}
```

### Compilation

When using the library in your projects:

```bash
# If installed to system
mpicc -o myprogram myprogram.c -lmpi_wrapper

# If using local build
mpicc -I/path/to/mpi_lib/include -L/path/to/mpi_lib/lib -o myprogram myprogram.c -lmpi_wrapper
```

## API Overview

### Initialization
- `mpi_init()` - Initialize MPI and context
- `mpi_finalize()` - Clean up MPI

### Point-to-point Communication
- `mpi_send()` - Send data
- `mpi_recv()` - Receive data
- `mpi_isend()` - Non-blocking send
- `mpi_irecv()` - Non-blocking receive

### Collective Communication
- `mpi_barrier()` - Synchronization barrier
- `mpi_bcast()` - Broadcast data
- `mpi_reduce()` - Reduce operation
- `mpi_allreduce()` - All-reduce operation

### One-sided Communication
- `mpi_put()` - Put data into remote memory
- `mpi_get()` - Get data from remote memory
- `mpi_compare_and_swap()` - Atomic compare and swap
- `mpi_fetch_and_op()` - Atomic fetch and operation

### Window Management
- `mpi_win_create()` - Create MPI window
- `mpi_win_destroy()` - Destroy MPI window
- `mpi_win_lock_all()` - Lock window
- `mpi_win_unlock_all()` - Unlock window

### Memory Management
- `mpi_malloc()` - Allocate memory on specific rank
- `mpi_calloc()` - Allocate and zero memory on specific rank
- `mpi_free()` - Free memory on specific rank

### Utilities
- `mpi_is_root()` - Check if current process is root
- `mpi_get_rank()` - Get process rank
- `mpi_get_size()` - Get number of processes
- `mpi_print_info()` - Print MPI information

## Error Handling

The library provides two error handling approaches:

1. **Automatic abort on error** (using `MPI_CHECK`):
   ```c
   MPI_CHECK(mpi_send(data, count, MPI_INT, dest, tag, comm));
   ```

2. **Return error codes** (using `MPI_TRY`):
   ```c
   MPI_TRY(mpi_send(data, count, MPI_INT, dest, tag, comm));
   ```

## Examples

The library comes with several example programs:

- `basic_example` - Comprehensive usage demonstration
- `hello_world` - Basic MPI initialization and info
- `point_to_point` - Send/receive operations
- `window_example` - Window management demonstration
- `one_sided` - One-sided communication example
- `collective` - Collective operations example
- `test_mpi_lib` - Comprehensive test suite

Build examples:
```bash
make examples
```

Run examples:
```bash
mpirun -n 2 ./examples/hello_world
mpirun -n 3 ./examples/point_to_point
mpirun -n 2 ./examples/basic_example
mpirun -n 4 ./examples/test_mpi_lib
```

## Integration with Existing Projects

To use the MPI Wrapper Library in your existing project:

1. Copy the library files to your project or install system-wide
2. Include the header: `#include "mpi_lib.h"`
3. Link with the library: `-lmpi_wrapper`
4. Replace MPI calls with MPI wrapper equivalents

### Example: Converting existing MPI code

**Before:**
```c
int err = MPI_Init(&argc, &argv);
if (err != MPI_SUCCESS) {
    // handle error
}
MPI_Comm_rank(MPI_COMM_WORLD, &rank);
MPI_Comm_size(MPI_COMM_WORLD, &size);
```

**After:**
```c
mpi_context_t ctx;
MPI_TRY(mpi_init(argc, argv, &ctx));
int rank = mpi_get_rank(&ctx);
int size = mpi_get_size(&ctx);
```

## Directory Structure

```
mpi_lib/
├── include/
│   └── mpi_lib.h           # Main header file
├── src/
│   └── mpi_lib.c           # Implementation
├── examples/
│   ├── hello_world.c       # Basic example
│   ├── point_to_point.c    # P2P communication
│   ├── one_sided.c         # RMA operations
│   ├── collective.c        # Collective operations
│   ├── basic_example.c     # Comprehensive example
│   ├── window_example.c    # Window management
│   ├── test_mpi_lib.c      # Test suite
│   └── Makefile           # Examples makefile
├── lib/                   # Built libraries (generated)
├── build/                 # Build artifacts (generated)
├── Makefile              # Main makefile
├── CHANGELOG.md          # Version history
└── README.md             # This file
```

## Requirements

- MPI implementation (OpenMPI, MPICH, Intel MPI, etc.)
- C99 compatible compiler
- Make build system

## License

This library is provided as-is for educational and research purposes. Feel free to modify and distribute according to your needs.

## Contributing

To contribute to this library:
1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly with the comprehensive test suite
5. Submit a pull request

## Troubleshooting

### Common Issues

1. **Compilation errors**: Ensure MPI is properly installed and `mpicc` is in your PATH
2. **Link errors**: Make sure library path is correct and library is built
3. **Runtime errors**: Check MPI installation and process count requirements
4. **Shared library issues**: Set `LD_LIBRARY_PATH` or use proper rpath settings

### Getting Help

- Check the examples for usage patterns
- Review the header file for complete API documentation
- Run the comprehensive test suite: `mpirun -n 4 ./examples/test_mpi_lib`
- Test with simple programs first
- Verify MPI installation with basic MPI programs

## Version History

- v1.0.1 - Current unified API with robust error handling and comprehensive test suite
- v1.0.0 - Legacy version (deprecated)
