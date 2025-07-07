#!/bin/bash

# SPMC Queue Benchmark Runner
# Automated benchmark execution with different configurations

set -e

# Configuration
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

# Helper functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_separator() {
    echo "================================================================"
}

print_usage() {
    echo ""
    echo "SPMC Queue Benchmark Runner"
    echo "=========================="
    echo ""
    echo "Usage: $0 [OPTIONS] [TEST_TYPE]"
    echo ""
    echo "Test Types:"
    echo "  quick       - Quick validation test (default)"
    echo "  throughput  - Throughput benchmark"
    echo "  latency     - Latency analysis"
    echo "  scalability - Scalability testing"
    echo "  stress      - Stress testing"
    echo "  suite       - Run complete benchmark suite"
    echo "  all         - Same as suite"
    echo ""
    echo "Options:"
    echo "  -p, --processes NUM   Number of MPI processes (default: 3)"
    echo "  -o, --output DIR      Output directory for results"
    echo "  -l, --log-level LEVEL Log level (info, warning, error)"
    echo "  -h, --help           Show this help message"
    echo "  -v, --verbose        Enable verbose output"
    echo "  --no-build           Skip build step"
    echo "  --export-csv         Export results to CSV"
    echo ""
    echo "Examples:"
    echo "  $0 quick"
    echo "  $0 throughput -p 4"
    echo "  $0 suite -p 6 --export-csv"
    echo "  $0 scalability -o /tmp/results"
    echo ""
}

# Default values
NUM_PROCESSES=3
TEST_TYPE="quick"
OUTPUT_DIR=""
VERBOSE=false
NO_BUILD=false
EXPORT_CSV=false

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -p|--processes)
            NUM_PROCESSES="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        -h|--help)
            print_usage
            exit 0
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        --no-build)
            NO_BUILD=true
            shift
            ;;
        --export-csv)
            EXPORT_CSV=true
            shift
            ;;
        quick|throughput|latency|scalability|stress|suite|all)
            TEST_TYPE="$1"
            shift
            ;;
        *)
            log_error "Unknown option: $1"
            print_usage
            exit 1
            ;;
    esac
done

# Set default output directory
if [[ -z "$OUTPUT_DIR" ]]; then
    OUTPUT_DIR="$RESULTS_DIR"
fi

# Create output directories
mkdir -p "$OUTPUT_DIR" "$LOG_DIR"

# Function to run a single benchmark
run_benchmark() {
    local test_type="$1"
    local num_procs="$2"
    local timestamp=$(date +"%Y%m%d_%H%M%S")
    local log_file="${LOG_DIR}/benchmark_${test_type}_${num_procs}procs_${timestamp}.log"
    
    log_info "Running $test_type benchmark with $num_procs processes..."
    
    # Build MPI command
    local mpi_cmd="mpirun"
    
    # Add WSL/root permissions if needed
    if [[ -n "${WSL_DISTRO_NAME}" ]] || [[ "$EUID" -eq 0 ]]; then
        mpi_cmd="$mpi_cmd --allow-run-as-root"
    fi
    
    mpi_cmd="$mpi_cmd -np $num_procs $EXAMPLE_BIN $test_type"
    
    # Execute benchmark
    if [[ "$VERBOSE" == "true" ]]; then
        log_info "Executing: $mpi_cmd"
        $mpi_cmd 2>&1 | tee "$log_file"
    else
        $mpi_cmd > "$log_file" 2>&1
    fi
    
    local exit_code=$?
    
    if [[ $exit_code -eq 0 ]]; then
        log_success "$test_type benchmark completed successfully"
        
        # Move CSV results if they exist
        if [[ "$EXPORT_CSV" == "true" ]]; then
            local csv_file="benchmark_${test_type}_${num_procs}procs.csv"
            if [[ -f "${BENCHMARK_DIR}/examples/$csv_file" ]]; then
                mv "${BENCHMARK_DIR}/examples/$csv_file" "$OUTPUT_DIR/"
                log_info "Results exported to: $OUTPUT_DIR/$csv_file"
            fi
        fi
    else
        log_error "$test_type benchmark failed (exit code: $exit_code)"
        log_error "Check log file: $log_file"
        return $exit_code
    fi
}

# Function to run benchmark suite
run_benchmark_suite() {
    log_info "Running complete benchmark suite..."
    print_separator
    
    local tests=("quick" "throughput" "latency")
    local process_counts=(3 4 6)
    
    for test in "${tests[@]}"; do
        for procs in "${process_counts[@]}"; do
            run_benchmark "$test" "$procs"
            echo ""
        done
    done
    
    # Run scalability test with multiple process counts
    log_info "Running scalability analysis..."
    for procs in 3 4 5 6 8; do
        run_benchmark "scalability" "$procs"
    done
    
    log_success "Benchmark suite completed!"
}

# Main execution
main() {
    print_separator
    log_info "SPMC Queue Benchmark Runner"
    print_separator
    
    # Build benchmark if needed
    if [[ "$NO_BUILD" != "true" ]]; then
        log_info "Building benchmark library and examples..."
        cd "$BENCHMARK_DIR"
        if make clean && make all; then
            log_success "Build completed successfully"
        else
            log_error "Build failed"
            exit 1
        fi
        echo ""
    fi
    
    # Check if benchmark executable exists
    if [[ ! -x "$EXAMPLE_BIN" ]]; then
        log_error "Benchmark executable not found: $EXAMPLE_BIN"
        log_error "Run 'make examples' to build the benchmark program"
        exit 1
    fi
    
    # Create output directory
    mkdir -p "$OUTPUT_DIR"
    log_info "Results will be saved to: $OUTPUT_DIR"
    echo ""
    
    # Run requested benchmark(s)
    case "$TEST_TYPE" in
        suite|all)
            run_benchmark_suite
            ;;
        *)
            run_benchmark "$TEST_TYPE" "$NUM_PROCESSES"
            ;;
    esac
    
    print_separator
    log_success "All benchmarks completed successfully!"
    log_info "Log files saved to: $LOG_DIR"
    if [[ "$EXPORT_CSV" == "true" ]]; then
        log_info "CSV results saved to: $OUTPUT_DIR"
    fi
    print_separator
}

# Run main function
main "$@"
