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
    echo -e "${BLUE}[INFO]${NC} $1" >&2
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1" >&2
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1" >&2
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
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
    echo "  -s, --spmc-path PATH  Path to SPMC implementation directory"
    echo "  -o, --output DIR      Output directory for results"
    echo "  -l, --log-level LEVEL Log level (info, warning, error)"
    echo "  -h, --help           Show this help message"
    echo "  -v, --verbose        Enable verbose output"
    echo "  --no-build           Skip build step"
    echo "  --export-csv         Export results to CSV"
    echo "  --max-tests NUM      Maximum number of tests for suite (default: 6)"
    echo "  --quick-suite        Run only essential tests (2-3 tests)"
    echo ""
    echo "Examples:"
    echo "  $0 quick"
    echo "  $0 throughput -p 4"
    echo "  $0 throughput -p 4 -s ../spmc_2004"
    echo "  $0 suite -p 6 --export-csv -s /path/to/spmc_impl"
    echo "  $0 scalability -o /tmp/results"
    echo ""
}

# Default values
NUM_PROCESSES=3
TEST_TYPE="quick"
OUTPUT_DIR=""
SPMC_PATH=""
VERBOSE=false
NO_BUILD=false
EXPORT_CSV=false
MAX_TESTS=6
QUICK_SUITE=false

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -p|--processes)
            NUM_PROCESSES="$2"
            shift 2
            ;;
        -s|--spmc-path)
            SPMC_PATH="$2"
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
        --max-tests)
            MAX_TESTS="$2"
            shift 2
            ;;
        --quick-suite)
            QUICK_SUITE=true
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

# Function to detect available SPMC types
detect_spmc_types() {
    local workspace_dir="${BENCHMARK_DIR}/.."
    local spmc_types=()
    
    # Look for directories that contain SPMC implementations
    for dir in "${workspace_dir}"/spmc_*/; do
        if [[ -d "$dir" ]]; then
            local dirname=$(basename "$dir")
            spmc_types+=("$dirname")
        fi
    done
    
    # Also check for other common SPMC directory patterns
    for dir in "${workspace_dir}"/spmc*/; do
        if [[ -d "$dir" ]]; then
            local dirname=$(basename "$dir")
            if [[ "$dirname" != "spmc" && "$dirname" != spmc_* ]]; then
                spmc_types+=("$dirname")
            fi
        fi
    done
    
    echo "${spmc_types[@]}"
}

# Function to select SPMC implementation
select_spmc_implementation() {
    # If SPMC path was specified via command line, validate and use it
    if [[ -n "$SPMC_PATH" ]]; then
        # Handle relative paths properly
        if [[ "$SPMC_PATH" == /* ]]; then
            # Absolute path
            local abs_path="$SPMC_PATH"
        else
            # Relative path - resolve from current directory
            local abs_path=$(realpath "$SPMC_PATH" 2>/dev/null || echo "$SPMC_PATH")
        fi
        
        if [[ ! -d "$abs_path" ]]; then
            log_error "Specified SPMC path '$SPMC_PATH' does not exist"
            log_error "Resolved to: $abs_path"
            exit 1
        fi
        
        local spmc_name=$(basename "$abs_path")
        log_info "Using specified SPMC implementation: $spmc_name at $abs_path"
        # Force output to be flushed before returning
        sleep 0.1
        echo "$abs_path"
        return
    fi
    
    local available_types=($(detect_spmc_types))
    
    if [[ ${#available_types[@]} -eq 0 ]]; then
        log_error "No SPMC implementations found in workspace"
        log_info "Use -s option to specify SPMC implementation path"
        exit 1
    elif [[ ${#available_types[@]} -eq 1 ]]; then
        local spmc_path="${BENCHMARK_DIR}/../${available_types[0]}"
        log_info "Found single SPMC implementation: ${available_types[0]}"
        echo "$spmc_path"
    else
        log_info "Multiple SPMC implementations found:"
        for i in "${!available_types[@]}"; do
            echo "  $((i+1)). ${available_types[$i]}"
        done
        
        while true; do
            read -p "Select SPMC type (1-${#available_types[@]}): " choice
            if [[ "$choice" =~ ^[0-9]+$ ]] && [[ "$choice" -ge 1 ]] && [[ "$choice" -le ${#available_types[@]} ]]; then
                local selected_type="${available_types[$((choice-1))]}"
                local spmc_path="${BENCHMARK_DIR}/../$selected_type"
                echo "$spmc_path"
                break
            else
                log_error "Invalid selection. Please choose 1-${#available_types[@]}"
            fi
        done
    fi
}

# Function to build SPMC implementation
build_spmc_implementation() {
    local spmc_path="$1"
    local spmc_name=$(basename "$spmc_path")
    
    echo "" >&2
    log_info "Building SPMC implementation: $spmc_name"
    log_info "SPMC path: $spmc_path"
    
    # Debug: Show what we're checking
    log_info "Checking if directory exists..."
    if [[ ! -d "$spmc_path" ]]; then
        log_error "SPMC directory does not exist: $spmc_path"
        log_info "Current working directory: $(pwd)"
        log_info "Attempting to list parent directory:"
        ls -la "$(dirname "$spmc_path")" 2>/dev/null || log_error "Cannot list parent directory"
        return 1
    fi
    
    log_info "Directory exists, checking for Makefile..."
    if [[ ! -f "$spmc_path/Makefile" ]]; then
        log_error "No Makefile found in $spmc_path"
        log_info "Contents of $spmc_path:"
        ls -la "$spmc_path" 2>/dev/null || log_error "Cannot list directory contents"
        return 1
    fi
    
    log_info "Found Makefile, starting build..."
    cd "$spmc_path" || {
        log_error "Failed to change directory to $spmc_path"
        return 1
    }
    
    if make clean && make all; then
        log_success "SPMC implementation built successfully"
        cd "$BENCHMARK_DIR"
        return 0
    else
        log_error "Build failed with make command"
        cd "$BENCHMARK_DIR"
        return 1
    fi
}
# Function to run a single benchmark
run_benchmark() {
    local test_type="$1"
    local num_procs="$2"
    local spmc_path="$3"
    local spmc_name=$(basename "$spmc_path")
    local timestamp=$(date +"%Y%m%d_%H%M%S")
    local log_file="${LOG_DIR}/benchmark_${test_type}_${spmc_name}_${num_procs}procs_${timestamp}.log"
    
    # Create subdirectory for this SPMC type with timestamp and process count
    local spmc_results_dir="${OUTPUT_DIR}/${spmc_name}_${test_type}_${num_procs}procs_${timestamp}"
    mkdir -p "$spmc_results_dir"
    
    log_info "Running $test_type benchmark with $num_procs processes for $spmc_name..."
    
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
        
        # Always try to move CSV results if they exist (only standard CSV, not detailed)
        local csv_file="benchmark_${test_type}_${num_procs}procs.csv"
        local new_csv_name="${spmc_name}_${test_type}_${num_procs}procs_${timestamp}.csv"
        
        # Check if CSV file exists in benchmark directory (most likely location)
        if [[ -f "${BENCHMARK_DIR}/$csv_file" ]]; then
            mv "${BENCHMARK_DIR}/$csv_file" "$spmc_results_dir/$new_csv_name"
            log_info "Results moved to: $spmc_results_dir/$new_csv_name"
        # Also check examples directory (in case it's created there)
        elif [[ -f "${BENCHMARK_DIR}/examples/$csv_file" ]]; then
            mv "${BENCHMARK_DIR}/examples/$csv_file" "$spmc_results_dir/$new_csv_name"
            log_info "Results moved from examples to: $spmc_results_dir/$new_csv_name"
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
    
    # Reduced test set to prevent verbose overload
    local tests=("quick" "throughput")
    local process_counts=(3 4)
    
    local test_count=0
    local max_tests=$MAX_TESTS
    
    # Use reduced test set for quick suite or verbose mode
    if [[ "$QUICK_SUITE" == "true" ]] || [[ "$VERBOSE" == "true" ]]; then
        local tests=("quick")
        local process_counts=(3)
        max_tests=2
        log_info "Using quick suite mode (limited tests to prevent overload)"
    else
        # Reduced test set to prevent verbose overload
        local tests=("quick" "throughput")
        local process_counts=(3 4)
    fi
    
    for test in "${tests[@]}"; do
        for procs in "${process_counts[@]}"; do
            if [[ $test_count -ge $max_tests ]]; then
                log_warning "Reached maximum test limit ($max_tests) to prevent verbose overload"
                break 2
            fi
            
            log_info "Running test $((test_count + 1))/$max_tests: $test with $procs processes"
            run_benchmark "$test" "$procs" "$1"
            test_count=$((test_count + 1))
            echo ""
            
            # Add small delay between tests to prevent system overload
            if [[ "$VERBOSE" == "true" ]]; then
                sleep 2
            fi
        done
    done
    
    # Run one scalability test only
    if [[ $test_count -lt $max_tests ]]; then
        log_info "Running scalability test..."
        run_benchmark "scalability" "3" "$1"
        test_count=$((test_count + 1))
    fi
    
    log_success "Benchmark suite completed! ($test_count tests executed)"
}

# Main execution
main() {
    print_separator
    log_info "SPMC Queue Benchmark Runner"
    print_separator
    
    # Detect and select SPMC implementation
    local spmc_path=$(select_spmc_implementation)
    local spmc_name=$(basename "$spmc_path")
    log_info "Selected SPMC implementation: $spmc_name at $spmc_path"
    echo ""
    
    # Build SPMC implementation first
    if [[ "$NO_BUILD" != "true" ]]; then
        if ! build_spmc_implementation "$spmc_path"; then
            log_error "Failed to build SPMC implementation"
            exit 1
        fi
        echo ""
    fi
    
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
    log_info "Results will be organized by SPMC type in subdirectories"
    echo ""
    
    # Run requested benchmark(s)
    case "$TEST_TYPE" in
        suite|all)
            run_benchmark_suite "$spmc_path"
            ;;
        *)
            run_benchmark "$TEST_TYPE" "$NUM_PROCESSES" "$spmc_path"
            ;;
    esac
    
    print_separator
    log_success "All benchmarks completed successfully!"
    log_info "Log files saved to: $LOG_DIR"
    log_info "CSV results organized by SPMC type with timestamps in: $OUTPUT_DIR"
    if [[ "$EXPORT_CSV" == "true" ]]; then
        log_info "CSV results exported successfully"
    fi
    print_separator
}

# Run main function
main "$@"
