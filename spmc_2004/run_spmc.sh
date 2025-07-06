#!/bin/bash

# SPMC Queue Test Runner
# This script simplifies running the SPMC queue with proper library paths

cd "$(dirname "$0")"

# Set library path
export LD_LIBRARY_PATH="../mpi_lib/lib:$LD_LIBRARY_PATH"

# Default number of processes
NUM_PROCS=${1:-3}

echo "Running SPMC Queue with $NUM_PROCS processes..."
echo "Using MPI library from: ../mpi_lib/lib"
echo "----------------------------------------"

mpirun --allow-run-as-root -np "$NUM_PROCS" ./queue_spmc

echo "----------------------------------------"
echo "Test completed."
