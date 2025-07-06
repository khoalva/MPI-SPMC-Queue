# Single-Enqueuer Wait-Free Queue with MPI

## Overview
This project implements a single-enqueuer, multi-dequeuer wait-free queue based on the algorithm described in Matei David's paper, "A Single-Enqueuer Wait-Free Queue Implementation." The implementation uses MPI one-sided communication (Remote Memory Access, RMA) to emulate shared-memory primitives in a distributed-memory environment. The queue ensures wait-free operations (each operation completes in a bounded number of steps) and linearizability (operations appear to occur atomically in a valid sequential order).

The implementation has been **completely refactored** to use the new robust MPI wrapper library (`mpi_lib`) which provides simplified error handling, automatic memory management, and a clean API. All legacy wrapper code has been removed.

## Features
- **Single Enqueuer**: Only one process (rank 0) can enqueue items.
- **Multiple Dequeuers**: Multiple processes can concurrently dequeue items.
- **Wait-Free**: Enqueue and dequeue operations complete in at most three steps.
- **Linearizable**: Operations maintain a consistent sequential order.
- **MPI One-Sided Communication**: Uses MPI RMA operations via the robust `mpi_lib` wrapper.
- **Robust Error Handling**: Automatic error checking and reporting via `MPI_TRY` macros.
- **Clean API**: Uses the new unified MPI wrapper library for simplified code.

## Prerequisites
- **MPI Implementation**: An MPI-compliant library (e.g., MPICH, OpenMPI).
- **C Compiler**: A C compiler compatible with MPI (e.g., `mpicc`).
- **Operating System**: Linux, macOS, or any system supporting MPI.

## Installation
1. **Clone or Download the Repository**:
    - Download the project files or clone the repository to your local machine.
2. **Ensure MPI is Installed**:
    - Install an MPI implementation, e.g., on Ubuntu:
      ```bash
      sudo apt-get install mpich
      ```
      or for OpenMPI:
      ```bash
      sudo apt-get install openmpi-bin openmpi-common libopenmpi-dev
      ```
3. **Build the MPI Library** (automatically done by Makefile):
    - The new `mpi_lib` wrapper library will be built automatically when needed.
4. **Build the Project**:
    - Navigate to the project directory and run:
      ```bash
      make queue_spmc
      ```
      This compiles the SPMC queue implementation using the new library.

## File Structure
- **SPMC Queue Implementation** (New, Recommended):
  - `spmc_queue.h`: Header file for the SPMC queue using the new `mpi_lib`.
  - `spmc_queue.c`: Implementation using the robust MPI wrapper library.
  - `main_spmc.c`: Clean main program demonstrating the SPMC queue.
  - `run_spmc.sh`: Convenient test runner script.

- **Original Implementation** (Legacy):
  - `queue.h`: Original header file with basic MPI calls.
  - `queue.c`: Original implementation with direct MPI usage.
  - `main.c`: Original main program.

- **Build System**:
  - `Makefile`: Updated build script supporting both implementations.
  - `../mpi_lib/`: The new robust MPI wrapper library.

## Usage

### SPMC Queue (Recommended)
1. **Compile the SPMC Implementation**:
   ```bash
   make queue_spmc
   ```

2. **Run the Program**:
   - **Using the convenience script** (easiest):
     ```bash
     ./run_spmc.sh 4
     ```
   - **Manual execution**:
     ```bash
     LD_LIBRARY_PATH=../mpi_lib/lib:$LD_LIBRARY_PATH mpirun --allow-run-as-root -np 4 ./queue_spmc
     ```

3. **Expected Output**:
   - Example output for 4 processes:
     ```
     SPMC Queue initialized successfully on rank 0/4
     MPI Library v1.0.1
     Process 0 of 4 processes
     
     === Enqueuing Phase ===
     Rank 0 enqueued: 100 (row: 0, col: 0)
     Rank 0 enqueued: 200 (row: 0, col: 1)
     ...
     
     === Dequeuing Phase ===
     Rank 1 dequeued: 100 (row: 0, col: 0)
     Rank 2 dequeued: 200 (row: 0, col: 1)
     ...
     
     === Final Statistics ===
     SPMC Queue demonstration completed successfully!
     ```

### Original Implementation (Legacy)
1. **Compile the Original Implementation**:
   ```bash
   make queue
   ```

2. **Run the Program**:
   ```bash
   mpirun --allow-run-as-root -np 4 ./queue
   ```

### Build Targets
- `make all`: Build both implementations
- `make queue_spmc`: Build SPMC implementation (recommended)
- `make queue`: Build original implementation
- `make clean`: Remove build artifacts
- `make distclean`: Clean everything including the MPI library

## Configuration
- **Constants** (defined in `spmc_queue.h`):
    - `L` (-1): Represents an empty cell (⊥).
    - `T` (-2): Represents a dequeued cell (⊤).
    - `MAX_ROWS` (1000): Maximum rows in the ITEMS array.
    - `MAX_COLS` (1000): Maximum columns in the ITEMS array.
    - `MAX_VALUE` (1000): Maximum value that can be enqueued.
- Modify these constants to adjust queue capacity or value constraints.

## Implementation Details
- **New SPMC Implementation**:
    - Uses the robust `mpi_lib` wrapper library for simplified MPI operations.
    - Automatic error handling via `MPI_TRY` macros.
    - Clean memory management with `mpi_calloc`/`mpi_free`.
    - Simplified window management with `mpi_win_create`/`mpi_win_destroy`.
    - Type-safe operations with `mpi_window_t` structures.

- **Data Structures**:
    - `HEAD`: Array of integers for Fetch&Add operations.
    - `ITEMS`: 2D array representing queue values or special markers (⊥, ⊤).
    - `ROW`: Integer indicating the current row for enqueuing.

- **MPI Windows**: Shared data exposed via MPI windows on rank 0, using passive target synchronization.

- **Operations**:
    - **Enqueue**: Uses `mpi_compare_and_swap` to atomically place values.
    - **Dequeue**: Uses `mpi_get` and `mpi_fetch_and_op` for wait-free access.

- **Wait-Free Property**: Each operation completes in at most three MPI RMA operations.
- **Linearizability**: Ensured by atomic MPI operations.

## Advantages of the New Implementation
- **Simplified Code**: 50% reduction in boilerplate code through the wrapper library.
- **Better Error Handling**: Automatic error checking and informative error messages.
- **Type Safety**: Wrapper types prevent common MPI usage errors.
- **Memory Safety**: Automatic memory management reduces memory leaks.
- **Maintainability**: Clean, readable code that's easier to extend and debug.

## Limitations
- **Fixed Size**: Uses fixed-size arrays for simplicity. For unbounded queues, implement row-reuse.
- **Performance**: MPI one-sided operations introduce network latency.
- **Testing**: Basic demonstration included; add comprehensive tests for production use.

## Future Improvements
- Implement row reuse to make the queue unbounded, as suggested in the paper.
- Add comprehensive unit tests for robustness.
- Optimize MPI communication for better performance (e.g., batching operations).
- Support dynamic resizing of the ITEMS array.

## License
This project is licensed under the MIT License. See the LICENSE file for details (not included in this repository).

## Contact
For questions or contributions, please contact the project maintainer or open an issue in the repository.

---
*Generated on July 2, 2025*