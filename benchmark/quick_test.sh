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

# Create unique folder name with timestamp
timestamp=$(date +"%Y%m%d_%H%M%S")
SPMC_TYPE_DIR="${RESULTS_DIR}/benchmark_${TEST_TYPE}_${NUM_PROCESSES}procs_${timestamp}"

# Create directories
mkdir -p "$RESULTS_DIR" "$SPMC_TYPE_DIR" "$LOG_DIR"

log_info "Running single benchmark test..."
log_info "Test type: $TEST_TYPE"
log_info "Processes: $NUM_PROCESSES"
log_info "Verbose: $VERBOSE"
log_info "Results will be saved to: $(basename "$SPMC_TYPE_DIR")"

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
log_file="${LOG_DIR}/quick_test_${TEST_TYPE}_${NUM_PROCESSES}procs_${timestamp}.log"

mpi_cmd="mpirun"

# Add WSL/root permissions if needed
if [[ -n "${WSL_DISTRO_NAME}" ]] || [[ "$EUID" -eq 0 ]]; then
    mpi_cmd="$mpi_cmd --allow-run-as-root"
fi

mpi_cmd="$mpi_cmd -np $NUM_PROCESSES $EXAMPLE_BIN $TEST_TYPE"

log_info "Executing: $mpi_cmd"

# Change to SPMC type directory before running so CSV files are created there
cd "$SPMC_TYPE_DIR"

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
    
    # CSV files should now be created directly in SPMC type directory
    log_info "CSV files should be created in: $SPMC_TYPE_DIR"
    
    # Return to benchmark directory
    cd "$BENCHMARK_DIR"
    
    # Check if files were created in SPMC type directory
    csv_count=$(find "$SPMC_TYPE_DIR" -name "*.csv" -type f 2>/dev/null | wc -l)
    if [[ $csv_count -gt 0 ]]; then
        log_info "Found $csv_count CSV file(s) in SPMC type directory"
        find "$SPMC_TYPE_DIR" -name "*.csv" -type f -exec basename {} \; | while read filename; do
            log_info "Available: $filename"
        done
    else
        log_info "No CSV files found in SPMC type directory - checking benchmark dir"
        # Fallback: move only the standard CSV file (not detailed)
        csv_pattern="benchmark_${TEST_TYPE}_${NUM_PROCESSES}procs.csv"
        if [[ -f "${BENCHMARK_DIR}/$csv_pattern" ]]; then
            mv "${BENCHMARK_DIR}/$csv_pattern" "$SPMC_TYPE_DIR/"
            log_info "Moved: $csv_pattern"
        fi
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
