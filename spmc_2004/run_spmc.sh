#!/bin/bash

# SPMC Queue Test Runner
# This script simplifies running the SPMC queue with proper library paths

cd "$(dirname "$0")"

# Show usage if help requested
if [[ "$1" == "-h" || "$1" == "--help" ]]; then
    echo "Usage: $0 [num_processes]"
    echo ""
    echo "  num_processes  Number of MPI processes to run (default: 3)"
    echo ""
    echo "Examples:"
    echo "  $0          # Run with 3 processes"
    echo "  $0 4        # Run with 4 processes"
    echo "  $0 2        # Run with 2 processes (minimum)"
    echo ""
    echo "Requirements:"
    echo "  - At least 2 processes are required (1 enqueuer + 1+ dequeuers)"
    echo "  - MPI library must be built (../mpi_lib/lib/)"
    exit 0
fi

# Ensure library symlinks exist
LIB_DIR="../mpi_lib/lib"
if [ ! -L "$LIB_DIR/libmpi_wrapper.so.1.0.0" ]; then
    echo "Creating library symlinks..."
    cd "$LIB_DIR"
    ln -sf libmpi_wrapper.so libmpi_wrapper.so.1.0.0
    ln -sf libmpi_wrapper.so libmpi_wrapper.so.1
    cd - > /dev/null
fi

# Set library path
export LD_LIBRARY_PATH="../mpi_lib/lib:$LD_LIBRARY_PATH"

# Default number of processes
NUM_PROCS=${1:-3}

# Validate process count
if [ "$NUM_PROCS" -lt 2 ]; then
    echo "Error: At least 2 processes are required (1 enqueuer + 1+ dequeuers)"
    echo "Use: $0 --help for usage information"
    exit 1
fi

echo "Running SPMC Queue with $NUM_PROCS processes..."
echo "Using MPI library from: ../mpi_lib/lib"
echo "Library path: $LD_LIBRARY_PATH"
echo "----------------------------------------"

mpirun --allow-run-as-root -np "$NUM_PROCS" ./queue_spmc

echo "----------------------------------------"
echo "Test completed."
