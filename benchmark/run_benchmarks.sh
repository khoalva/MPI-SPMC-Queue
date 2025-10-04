#!/bin/bash

# SPMC Queue Benchmark Runner
# Automated benchmark execution with different configurations

set -e

# Configuration
BENCHMARK_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_DIR="$(cd "${BENCHMARK_DIR}/.." && pwd)"

# Add library path for dynamic linking - FIXED VERSION
export LD_LIBRARY_PATH="${BENCHMARK_DIR}/lib:${WORKSPACE_DIR}/mpi_lib/lib:/usr/local/lib:/usr/lib:/usr/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH"

RESULTS_DIR="${BENCHMARK_DIR}/results"
LOG_DIR="${BENCHMARK_DIR}/logs"
# Remove hardcoded EXAMPLE_BIN - will be set dynamically based on SPMC implementation

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
    echo "  --check-libs         Check library dependencies"
    echo ""
    echo "Examples:"
    echo "  $0 quick"
    echo "  $0 throughput -p 4"
    echo "  $0 throughput -p 4 -s ../spmc_2004"
    echo "  $0 suite -p 6 --export-csv -s ../spmc_impl"
    echo "  $0 scalability -o ./results"
    echo "  $0 --check-libs"
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
CHECK_LIBS=false

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
        --check-libs)
            CHECK_LIBS=true
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

# Function to check library dependencies
check_library_dependencies() {
    local executable="$1"
    
    if [[ ! -f "$executable" ]]; then
        log_error "Executable not found: $executable"
        return 1
    fi
    
    if ldd "$executable" 2>/dev/null | grep -q "not found"; then
        log_error "Missing library dependencies found:"
        ldd "$executable" 2>/dev/null | grep "not found"
        log_info "Current LD_LIBRARY_PATH: ${LD_LIBRARY_PATH:-'(not set)'}"
        return 1
    else
        log_success "Library dependencies check passed"
        return 0
    fi
}

# Function to debug path resolution
debug_paths() {
    log_info "=== Path Debug Information ==="
    log_info "Current working directory: $(pwd)"
    log_info "Benchmark directory: $BENCHMARK_DIR"
    log_info "Workspace directory: $WORKSPACE_DIR"
    log_info "Specified SPMC path: ${SPMC_PATH:-'(not specified)'}"
    log_info "Current LD_LIBRARY_PATH: ${LD_LIBRARY_PATH:-'(not set)'}"
    log_info "Available directories in workspace:"
    ls -la "${WORKSPACE_DIR}" | grep ^d || log_warning "Cannot list workspace directory"
    log_info "============================="
}

# Function to detect available SPMC types
detect_spmc_types() {
    local spmc_types=()
    
    log_info "Scanning for SPMC implementations in: $WORKSPACE_DIR"
    
    # Look for directories that contain SPMC implementations
    for dir in "${WORKSPACE_DIR}"/spmc_*/; do
        if [[ -d "$dir" && -f "$dir/Makefile" ]]; then
            local dirname=$(basename "$dir")
            spmc_types+=("$dirname")
            log_info "Found SPMC implementation: $dirname"
        fi
    done
    
    # Also check for other common SPMC directory patterns
    for dir in "${WORKSPACE_DIR}"/spmc*/; do
        if [[ -d "$dir" && -f "$dir/Makefile" ]]; then
            local dirname=$(basename "$dir")
            # Avoid duplicates
            if [[ ! " ${spmc_types[@]} " =~ " ${dirname} " ]]; then
                spmc_types+=("$dirname")
                log_info "Found SPMC implementation: $dirname"
            fi
        fi
    done
    
    echo "${spmc_types[@]}"
}

# Function to select SPMC implementation
select_spmc_implementation() {
    # If SPMC path was specified via command line, validate and use it
    if [[ -n "$SPMC_PATH" ]]; then
        local resolved_path=""
        
        # Handle different path formats
        if [[ "$SPMC_PATH" == /* ]]; then
            # Absolute path
            resolved_path="$SPMC_PATH"
        elif [[ "$SPMC_PATH" == ../* ]] || [[ "$SPMC_PATH" == ./* ]]; then
            # Relative path from current directory
            resolved_path=$(cd "$(dirname "$SPMC_PATH")" && pwd)/$(basename "$SPMC_PATH")
        else
            # Relative path - try from benchmark directory first
            if [[ -d "${BENCHMARK_DIR}/$SPMC_PATH" ]]; then
                resolved_path="${BENCHMARK_DIR}/$SPMC_PATH"
            elif [[ -d "${WORKSPACE_DIR}/$SPMC_PATH" ]]; then
                resolved_path="${WORKSPACE_DIR}/$SPMC_PATH"
            else
                # Try to resolve from current directory
                resolved_path=$(realpath "$SPMC_PATH" 2>/dev/null || echo "$SPMC_PATH")
            fi
        fi
        
        if [[ ! -d "$resolved_path" ]]; then
            log_error "Specified SPMC path '$SPMC_PATH' does not exist"
            log_error "Resolved to: $resolved_path"
            log_info "Current working directory: $(pwd)"
            log_info "Benchmark directory: $BENCHMARK_DIR"
            log_info "Workspace directory: $WORKSPACE_DIR"
            exit 1
        fi
        
        local spmc_name=$(basename "$resolved_path")
        log_info "Using specified SPMC implementation: $spmc_name"
        log_info "Resolved path: $resolved_path"
        echo "$resolved_path"
        return
    fi
    
    local available_types=($(detect_spmc_types))

    if [[ ${#available_types[@]} -eq 0 ]]; then
        log_error "No SPMC implementations found in workspace"
        log_info "Use -s option to specify SPMC implementation path"
        exit 1
    elif [[ ${#available_types[@]} -eq 1 ]]; then
        local spmc_path="${WORKSPACE_DIR}/${available_types[0]}"
        log_info "Found single SPMC implementation: ${available_types[0]}"
        echo "$spmc_path"
    else
        log_info "Multiple SPMC implementations found:"
        for i in "${!available_types[@]}"; do
            echo "  $((i+1)). ${available_types[$i]}" >&2
        done

        while true; do
            read -p "Select SPMC type (1-${#available_types[@]}): " choice
            if [[ "$choice" =~ ^[0-9]+$ ]] && [[ "$choice" -ge 1 ]] && [[ "$choice" -le ${#available_types[@]} ]]; then
                local selected_type="${available_types[$((choice-1))]}"
                local spmc_path="${WORKSPACE_DIR}/$selected_type"
                echo "$spmc_path"
                break
            else
                log_error "Invalid selection. Please choose 1-${#available_types[@]}"
            fi
        done
    fi
}

# Function to find SPMC executable
find_spmc_executable() {
    local spmc_path="$1"
    local spmc_name=$(basename "$spmc_path")
    
    # Common executable names to look for
    local possible_names=(
        "spmc_benchmark"
        "benchmark"
        "spmc_test"
        "test"
        "main"
        "spmc"
        "queue_spmc"
        "spmc_queue"
        "${spmc_name}_benchmark"
        "${spmc_name}_test"
        "${spmc_name}"
    )
    
    # Look for executables in the SPMC directory
    for name in "${possible_names[@]}"; do
        local exe_path="${spmc_path}/${name}"
        if [[ -x "$exe_path" ]]; then
            echo "$exe_path"
            return 0
        fi
    done
    
    # Look for executables in common subdirectories
    for subdir in "examples" "bin" "build" "test"; do
        if [[ -d "${spmc_path}/${subdir}" ]]; then
            for name in "${possible_names[@]}"; do
                local exe_path="${spmc_path}/${subdir}/${name}"
                if [[ -x "$exe_path" ]]; then
                    echo "$exe_path"
                    return 0
                fi
            done
        fi
    done
    
    # If not found, return empty string
    echo ""
    return 1
}

# Function to build SPMC implementation
build_spmc_implementation() {
    local spmc_path="$1"
    local spmc_name=$(basename "$spmc_path")
    
    echo "" >&2
    log_info "Building SPMC implementation: $spmc_name"
    
    if [[ ! -d "$spmc_path" ]]; then
        log_error "SPMC directory does not exist: $spmc_path"
        return 1
    fi
    
    if [[ ! -f "$spmc_path/Makefile" ]]; then
        log_error "No Makefile found in $spmc_path"
        return 1
    fi
    cd "$spmc_path" || {
        log_error "Failed to change directory to $spmc_path"
        return 1
    }
    
    # Try different build targets
    local build_targets=("all" "examples" "benchmark" "test")
    local build_success=false
    
    # Clean first
    make clean 2>/dev/null
    
    # Try each build target
    for target in "${build_targets[@]}"; do
        if make "$target" 2>/dev/null; then
            log_success "Successfully built target: $target"
            build_success=true
            break
        fi
    done
    
    cd "$BENCHMARK_DIR"
    
    if [[ "$build_success" == "true" ]]; then
        log_success "SPMC implementation built successfully"
        return 0
    else
        log_error "All build attempts failed"
        return 1
    fi
}

# Function to build and link SPMC object with benchmark object
build_and_link_spmc_benchmark() {
    local spmc_path="$1"
    local spmc_name=$(basename "$spmc_path")
    local benchmark_obj="${BENCHMARK_DIR}/examples/spmc_benchmark.o"
    local exe_out="${BENCHMARK_DIR}/bin/benchmark_${spmc_name}"

    # Tìm file object ở hai vị trí phổ biến
    local spmc_obj=""
    if [[ -f "${spmc_path}/build/spmc_queue.o" ]]; then
        spmc_obj="${spmc_path}/build/spmc_queue.o"
    elif [[ -f "${spmc_path}/spmc_queue.o" ]]; then
        spmc_obj="${spmc_path}/spmc_queue.o"
    else
        log_error "SPMC object file not found in build/ or root: ${spmc_path}"
        return 1
    fi

    mkdir -p "${BENCHMARK_DIR}/bin"
    mpicc -c "${BENCHMARK_DIR}/examples/spmc_benchmark.c" -I"$spmc_path" -I"$spmc_path/src" -I"${WORKSPACE_DIR}/mpi_lib/include" -o "$benchmark_obj"
    if [[ $? -ne 0 ]]; then
        log_error "Failed to build benchmark object file"
        return 1
    fi

    mpicc "$benchmark_obj" "$spmc_obj" \
        -o "$exe_out" \
        -I"$spmc_path" \
        -I"$spmc_path/src" \
        -I"${WORKSPACE_DIR}/mpi_lib/include" \
        -L"${WORKSPACE_DIR}/mpi_lib/lib" \
        -L"${BENCHMARK_DIR}/lib" \
        -lbenchmark -lmpi_wrapper -lmpi
    if [[ $? -ne 0 ]]; then
        log_error "Failed to link benchmark executable"
        return 1
    fi
    log_success "Benchmark executable created: $exe_out"
    echo "$exe_out"
}

# Function to run a single benchmark
run_benchmark() {
    local test_type="$1"
    local num_procs="$2"
    local spmc_path="$3"
    local session_folder="$4"
    local spmc_name=$(basename "$spmc_path")
    local timestamp=$(date +"%Y%m%d_%H%M%S")
    local log_file="${LOG_DIR}/benchmark_${test_type}_${spmc_name}_${num_procs}procs_${timestamp}.log"

    log_info "Running $test_type benchmark with $num_procs processes for $spmc_name..."

    # Build and link SPMC object with benchmark object
    local spmc_executable=$(build_and_link_spmc_benchmark "$spmc_path")
    if [[ -z "$spmc_executable" ]]; then
        log_error "Failed to build benchmark executable for $spmc_name"
        return 1
    fi


    
    # Check library dependencies before running
    if ! check_library_dependencies "$spmc_executable"; then
        log_error "Library dependency check failed"
        return 1
    fi

    # Build MPI command - detect MPI implementation
    local mpi_cmd=""
    local mpi_version=$(mpirun --version 2>&1 | head -1)
    
    if echo "$mpi_version" | grep -qi "open.mpi\|openmpi"; then
        # OpenMPI
        mpi_cmd="mpirun"
        if [[ -n "${WSL_DISTRO_NAME}" ]] || [[ "$EUID" -eq 0 ]]; then
            mpi_cmd="$mpi_cmd --allow-run-as-root"
        fi
    elif echo "$mpi_version" | grep -qi "mpich\|hydra"; then
        # MPICH
        mpi_cmd="mpirun"
    else
        # Fallback - try without special flags
        mpi_cmd="mpirun"
        log_warning "Unknown MPI implementation, using basic mpirun"
    fi
    
    mpi_cmd="$mpi_cmd -np $num_procs $spmc_executable $test_type"

    # Execute benchmark
    log_info "Executing: $mpi_cmd"
    
    # Change to session directory so CSV files are created there
    local original_dir=$(pwd)
    cd "$session_folder" || {
        log_error "Failed to change to session directory: $session_folder"
        return 1
    }
    
    if [[ "$VERBOSE" == "true" ]]; then
        if echo "$mpi_version" | grep -qi "open.mpi\|openmpi"; then
            OMPI_ALLOW_RUN_AS_ROOT=1 OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1 timeout 60 $mpi_cmd 2>&1 | tee "$log_file"
        else
            timeout 60 $mpi_cmd 2>&1 | tee "$log_file"
        fi
    else
        if echo "$mpi_version" | grep -qi "open.mpi\|openmpi"; then
            timeout 60 bash -c "OMPI_ALLOW_RUN_AS_ROOT=1 OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1 $mpi_cmd > '$log_file' 2>&1"
        else
            timeout 60 bash -c "$mpi_cmd > '$log_file' 2>&1"
        fi
    fi
    
    # Return to original directory
    cd "$original_dir"

    local exit_code=$?
    
    if [[ $exit_code -eq 124 ]]; then
        log_error "$test_type benchmark timed out after 60 seconds"
        log_error "Check log file for details: $log_file"
        return 124
    elif [[ $exit_code -eq 0 ]]; then
        log_success "$test_type benchmark completed successfully"
        log_info "Benchmark log file: $log_file"

        # Look for CSV results - should be in session folder now
        local csv_file="benchmark_${test_type}_${num_procs}procs.csv"
        local new_csv_name="${spmc_name}_${test_type}_${num_procs}procs_${timestamp}.csv"
        local csv_found=false

        # Primary location: session folder (where we executed MPI command)
        local primary_csv_path="${session_folder}/$csv_file"
        
        if [[ -f "$primary_csv_path" ]]; then
            log_info "Found CSV file in session folder: $primary_csv_path"
            mv "$primary_csv_path" "${session_folder}/$new_csv_name"
            log_success "Results renamed to: ${session_folder}/$new_csv_name"
            csv_found=true
        else
            log_warning "CSV file not found in session folder, searching other locations..."
            
            # Fallback locations for CSV files
            local search_paths=(
                "${BENCHMARK_DIR}/$csv_file"            # Benchmark directory
                "${BENCHMARK_DIR}/examples/$csv_file"   # Benchmark examples directory
                "${spmc_path}/$csv_file"               # SPMC implementation directory
                "${spmc_path}/examples/$csv_file"      # SPMC examples directory
                "${spmc_path}/bin/$csv_file"           # SPMC bin directory
                "${spmc_path}/build/$csv_file"         # SPMC build directory
                "$(dirname "$spmc_executable")/$csv_file" # Directory containing executable
            )

            for csv_path in "${search_paths[@]}"; do
                if [[ -f "$csv_path" ]]; then
                    log_info "Found CSV file at fallback location: $csv_path"
                    mv "$csv_path" "${session_folder}/$new_csv_name"
                    log_success "Results saved to: ${session_folder}/$new_csv_name"
                    csv_found=true
                    break
                fi
            done
        fi

        if [[ "$csv_found" == "false" ]]; then
            log_warning "CSV results file not found - check log file for details"
        fi
    else
        log_error "$test_type benchmark failed (exit code: $exit_code)"
        log_error "Check log file: $log_file"
        return $exit_code
    fi
}

# Function to run benchmark suite
run_benchmark_suite() {
    local spmc_path="$1"
    local session_folder="$2"
    
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
            run_benchmark "$test" "$procs" "$spmc_path" "$session_folder"
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
        run_benchmark "scalability" "3" "$spmc_path" "$session_folder"
        test_count=$((test_count + 1))
    fi
    
    log_success "Benchmark suite completed! ($test_count tests executed)"
}

# Main execution
main() {
    print_separator
    log_info "SPMC Queue Benchmark Runner"
    print_separator
    
    # If only checking libraries, do that and exit
    if [[ "$CHECK_LIBS" == "true" ]]; then
        log_info "Library check mode enabled"
        debug_paths
        # Try to find any existing executable to check
        local test_exe="${BENCHMARK_DIR}/bin/benchmark_spmc_2004"
        if [[ -f "$test_exe" ]]; then
            check_library_dependencies "$test_exe"
        else
            log_info "No existing executable found for library check"
            log_info "Build an executable first, then run with --check-libs"
        fi
        exit 0
    fi
    
    # Debug path information if verbose
    if [[ "$VERBOSE" == "true" ]]; then
        debug_paths
    fi
    
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
    
    # Find and verify SPMC executable
    local spmc_executable=$(find_spmc_executable "$spmc_path")
    if [[ -z "$spmc_executable" ]]; then
        log_error "No executable found in SPMC implementation: $spmc_path"
        log_error "Please ensure your SPMC implementation builds an executable"
        exit 1
    fi
    
    # Create output directory
    mkdir -p "$OUTPUT_DIR"
    
    # Create session folder for this benchmark run
    local session_timestamp=$(date +"%Y%m%d_%H%M%S")
    local spmc_name=$(basename "$spmc_path")
    local session_folder="${OUTPUT_DIR}/session_${spmc_name}_${session_timestamp}"
    mkdir -p "$session_folder"
    log_info "Results will be saved to: $session_folder"
    echo ""
    
    # Run requested benchmark(s)
    case "$TEST_TYPE" in
        suite|all)
            run_benchmark_suite "$spmc_path" "$session_folder"
            ;;
        *)
            run_benchmark "$TEST_TYPE" "$NUM_PROCESSES" "$spmc_path" "$session_folder"
            ;;
    esac
    
    print_separator
    log_success "All benchmarks completed successfully!"
    log_info "Log files saved to: $LOG_DIR"
    log_info "CSV results saved to session folder: $session_folder"
    if [[ "$EXPORT_CSV" == "true" ]]; then
        log_info "CSV results exported successfully"
    fi
    print_separator
}

# Run main function
main "$@"