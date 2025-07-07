#!/bin/bash

# Quick Test Script - Simplified version of run_benchmarks.sh
# Only runs a single quick test to avoid the verbose overload

set -e

BENCHMARK_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_DIR="${BENCHMARK_DIR}/results"
LOG_DIR="${BENCHMARK_DIR}/logs"
EXAMPLE_BIN="${BENCHMARK_DIR}/examples/spmc_benchmark"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Default configuration
NUM_PROCESSES=${1:-3}
TEST_TYPE=${2:-"quick"}
VERBOSE=${3:-false}
EXPORT_CSV=${4:-false}

# Create directories
mkdir -p "$RESULTS_DIR" "$LOG_DIR"

log_info "Running single benchmark test..."
log_info "Test type: $TEST_TYPE"
log_info "Processes: $NUM_PROCESSES"
log_info "Verbose: $VERBOSE"

# Check if benchmark executable exists
if [[ ! -x "$EXAMPLE_BIN" ]]; then
    log_error "Benchmark executable not found: $EXAMPLE_BIN"
    log_error "Building benchmark first..."
    cd "$BENCHMARK_DIR"
    make clean && make all
    if [[ ! -x "$EXAMPLE_BIN" ]]; then
        log_error "Build failed"
        exit 1
    fi
fi

# Build MPI command
timestamp=$(date +"%Y%m%d_%H%M%S")
log_file="${LOG_DIR}/quick_test_${TEST_TYPE}_${NUM_PROCESSES}procs_${timestamp}.log"

mpi_cmd="mpirun"

# Add WSL/root permissions if needed
if [[ -n "${WSL_DISTRO_NAME}" ]] || [[ "$EUID" -eq 0 ]]; then
    mpi_cmd="$mpi_cmd --allow-run-as-root"
fi

mpi_cmd="$mpi_cmd -np $NUM_PROCESSES $EXAMPLE_BIN $TEST_TYPE"

log_info "Executing: $mpi_cmd"

# Execute benchmark
if [[ "$VERBOSE" == "true" ]]; then
    $mpi_cmd 2>&1 | tee "$log_file"
else
    $mpi_cmd > "$log_file" 2>&1
fi

exit_code=$?

if [[ $exit_code -eq 0 ]]; then
    log_success "Benchmark completed successfully"
    
    # Show log content
    echo ""
    echo "===== BENCHMARK RESULTS ====="
    tail -20 "$log_file"
    echo ""
    
    # Always try to move CSV results if they exist (regardless of EXPORT_CSV flag)
    csv_file="benchmark_${TEST_TYPE}_${NUM_PROCESSES}procs.csv"
    
    # Check if CSV file exists in benchmark directory (most likely location)
    if [[ -f "${BENCHMARK_DIR}/$csv_file" ]]; then
        mv "${BENCHMARK_DIR}/$csv_file" "$RESULTS_DIR/"
        log_info "Results moved to: $RESULTS_DIR/$csv_file"
    # Also check examples directory (in case it's created there)
    elif [[ -f "${BENCHMARK_DIR}/examples/$csv_file" ]]; then
        mv "${BENCHMARK_DIR}/examples/$csv_file" "$RESULTS_DIR/"
        log_info "Results moved from examples to: $RESULTS_DIR/$csv_file"
    fi
    
    log_info "Full log saved to: $log_file"
else
    log_error "Benchmark failed (exit code: $exit_code)"
    log_error "Check log file: $log_file"
    echo ""
    echo "===== ERROR LOG ====="
    tail -10 "$log_file"
    exit $exit_code
fi
