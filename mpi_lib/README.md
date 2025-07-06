# MPI Wrapper Library

A simplified, robust wrapper library for MPI (Message Passing Interface) that makes MPI programming easier and more reliable.

## Overview

This library provides a simplified interface to MPI with the following benefits:

- **Simplified API**: Reduced parameter counts with sensible defaults
- **Robust Error Handling**: Automatic error detection and reporting
- **Memory Safety**: Built-in validation and bounds checking
- **Window Management**: Easy-to-use one-sided communication helpers
- **Documentation**: Comprehensive API documentation and examples

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
mpicc -o myprogram myprogram.c -lmpi_easy

# If using local build
mpicc -I/path/to/mpi_lib/include -L/path/to/mpi_lib/lib -o myprogram myprogram.c -lmpi_easy
```

## API Overview

### Initialization
- `mpi_easy_init()` - Initialize MPI and context
- `mpi_easy_finalize()` - Clean up MPI

### Point-to-point Communication
- `mpi_easy_send()` - Send data
- `mpi_easy_recv()` - Receive data
- `mpi_easy_isend()` - Non-blocking send
- `mpi_easy_irecv()` - Non-blocking receive

### Collective Communication
- `mpi_easy_barrier()` - Synchronization barrier
- `mpi_easy_bcast()` - Broadcast data
- `mpi_easy_reduce()` - Reduce operation
- `mpi_easy_allreduce()` - All-reduce operation

### One-sided Communication
- `mpi_easy_put()` - Put data into remote memory
- `mpi_easy_get()` - Get data from remote memory
- `mpi_easy_compare_and_swap()` - Atomic compare and swap
- `mpi_easy_fetch_and_op()` - Atomic fetch and operation

### Window Management
- `mpi_easy_win_create()` - Create MPI window
- `mpi_easy_win_destroy()` - Destroy MPI window
- `mpi_easy_win_lock_all()` - Lock window
- `mpi_easy_win_unlock_all()` - Unlock window

### Memory Management
- `mpi_easy_malloc()` - Allocate memory on specific rank
- `mpi_easy_calloc()` - Allocate and zero memory on specific rank
- `mpi_easy_free()` - Free memory on specific rank

### Utilities
- `mpi_easy_is_root()` - Check if current process is root
- `mpi_easy_get_rank()` - Get process rank
- `mpi_easy_get_size()` - Get number of processes
- `mpi_easy_print_info()` - Print MPI information

## Error Handling

The library provides two error handling approaches:

1. **Automatic abort on error** (using `MPI_EASY_CHECK`):
   ```c
   MPI_EASY_CHECK(mpi_easy_send(data, count, MPI_INT, dest, tag, comm));
   ```

2. **Return error codes** (using `MPI_EASY_TRY`):
   ```c
   MPI_EASY_TRY(mpi_easy_send(data, count, MPI_INT, dest, tag, comm));
   ```

## Examples

The library comes with several example programs:

- `basic_example` - Comprehensive usage demonstration
- `hello_world` - Basic MPI initialization and info
- `point_to_point` - Send/receive operations
- `window_example` - Window management demonstration
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

To use MPI Easy in your existing project:

1. Copy the library files to your project or install system-wide
2. Include the header: `#include "mpi_easy.h"`
3. Link with the library: `-lmpi_easy`
4. Replace MPI calls with MPI Easy equivalents

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
mpi_easy_context_t ctx;
MPI_EASY_TRY(mpi_easy_init(argc, argv, &ctx));
int rank = mpi_easy_get_rank(&ctx);
int size = mpi_easy_get_size(&ctx);
```

## Directory Structure

```
mpi_lib/
├── include/
│   └── mpi_easy.h          # Main header file
├── src/
│   └── mpi_easy.c          # Implementation
├── examples/
│   ├── hello_world.c       # Basic example
│   ├── point_to_point.c    # P2P communication
│   ├── one_sided.c         # RMA operations
│   ├── collective.c        # Collective operations
│   └── Makefile           # Examples makefile
├── lib/                   # Built libraries (generated)
├── build/                 # Build artifacts (generated)
├── Makefile              # Main makefile
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
4. Test thoroughly
5. Submit a pull request

## Troubleshooting

### Common Issues

1. **Compilation errors**: Ensure MPI is properly installed and `mpicc` is in your PATH
2. **Link errors**: Make sure library path is correct and library is built
3. **Runtime errors**: Check MPI installation and process count requirements

### Getting Help

- Check the examples for usage patterns
- Review the header file for complete API documentation
- Test with simple programs first
- Verify MPI installation with basic MPI programs

## Version History

- v1.0.0 - Initial release with core functionality
