# Single-Enqueuer Wait-Free Queue with MPI

## Overview
This project implements a single-enqueuer, multi-dequeuer wait-free queue based on the algorithm described in Matei David's paper, "A Single-Enqueuer Wait-Free Queue Implementation." The implementation uses MPI one-sided communication (Remote Memory Access, RMA) to emulate shared-memory primitives in a distributed-memory environment. The queue ensures wait-free operations (each operation completes in a bounded number of steps) and linearizability (operations appear to occur atomically in a valid sequential order).

The implementation is modular, with separate files for the queue logic, main program, and build configuration. It includes error handling, documentation, and a Makefile for easy compilation.

## Features
- **Single Enqueuer**: Only one process (rank 0) can enqueue items.
- **Multiple Dequeuers**: Multiple processes can concurrently dequeue items.
- **Wait-Free**: Enqueue and dequeue operations complete in at most three steps.
- **Linearizable**: Operations maintain a consistent sequential order.
- **MPI One-Sided Communication**: Uses MPI RMA operations (MPI_Compare_and_swap, MPI_Fetch_and_op, MPI_Put, MPI_Get) for distributed-memory support.
- **Error Handling**: Checks for MPI errors and memory allocation failures.

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
3. **Build the Project**:
    - Navigate to the project directory and run:
      ```bash
      make
      ```
      This compiles the source files (`main.c`, `queue.c`) and generates the executable `queue`.

## File Structure
- `queue.h`: Header file defining constants, data structures, and function prototypes.
- `queue.c`: Implementation of queue initialization, enqueue, and dequeue operations.
- `main.c`: Main program demonstrating queue usage with example enqueues and dequeues.
- `Makefile`: Build script for compiling the project.
- `README.md`: This file, providing project documentation.

## Usage
1. **Compile the Program**:
   ```bash
   make
   ```
2. **Run the Program**:
    - Execute the program with at least two MPI processes (one enqueuer and one or more dequeuers):
      ```bash
      mpirun -np 4 ./queue
      ```
    - The program enqueues values 1, 2, 3 from rank 0 and attempts to dequeue from all ranks.
3. **Expected Output**:
    - Example output for 4 processes:
      ```
      Rank 0 enqueued: 1
      Rank 0 enqueued: 2
      Rank 0 enqueued: 3
      Rank 1 dequeued: 1
      Rank 2 dequeued: 2
      Rank 3 dequeued: 3
      Rank 0 found empty queue
      ```
    - The exact dequeued values per rank may vary due to concurrent dequeuing.

4. **Clean Up**:
    - Remove compiled files:
      ```bash
      make clean
      ```

## Configuration
- **Constants** (defined in `queue.h`):
    - `L` (-1): Represents an empty cell (\(\perp\)).
    - `T` (-2): Represents a dequeued cell (\(\top\)).
    - `MAX_ROWS` (1000): Maximum rows in the ITEMS array.
    - `MAX_COLS` (1000): Maximum columns in the ITEMS array.
    - `MAX_VALUE` (1000): Maximum value that can be enqueued.
- Modify these constants in `queue.h` to adjust queue capacity or value constraints.

## Implementation Details
- **Data Structures**:
    - `HEAD`: Array of integers for Fetch&Add operations.
    - `ITEMS`: 2D array of integers representing queue values or special markers (\(\perp\), \(\top\)).
    - `ROW`: Integer indicating the current row for enqueuing.
- **MPI Windows**: Shared data is exposed via MPI windows on rank 0, using passive target synchronization (MPI_Win_lock_all).
- **Operations**:
    - **Enqueue**: Uses MPI_Compare_and_swap to swap a value into ITEMS, moving to a new row if a dequeuer interferes.
    - **Dequeue**: Reads ROW, performs Fetch&Add on HEAD, and swaps a value out of ITEMS with \(\top\).
- **Wait-Free Property**: Each operation completes in at most three MPI RMA operations.
- **Linearizability**: Ensured by atomic MPI operations (MPI_Compare_and_swap, MPI_Fetch_and_op).

## Limitations
- **Fixed Size**: The implementation uses fixed-size arrays for simplicity. To support unbounded queues, implement the row-reuse scheme from the paper (using sequence numbers and timestamps).
- **Performance**: MPI one-sided operations may introduce latency compared to shared-memory systems.
- **Testing**: The main program includes a basic test. For production use, add comprehensive tests for edge cases (e.g., empty queue, concurrent dequeues).

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