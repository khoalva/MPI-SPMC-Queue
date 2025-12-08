#!/bin/bash

# run_microbenchmarks.sh
# Script to run micro benchmarks with different configurations
# 
# This script runs proper micro benchmarks that measure queue performance
# in high contention scenarios with fixed operations per consumer

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
BENCHMARK_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_DIR="$(cd "${BENCHMARK_DIR}/.." && pwd)"
RESULTS_DIR="${BENCHMARK_DIR}/results_micro"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
SESSION_DIR="${RESULTS_DIR}/session_${TIMESTAMP}"

# Default parameters
OPS_PER_CONSUMER=10000  # Default operations per consumer for reliable measurements
NUM_PROCESSES=5  # Default: 1 producer + 4 consumers
MPI_HOSTS=""
SPMC_PATH=""
VERBOSE=false
TIMEOUT=30  # Default timeout: 30 seconds
REPEAT=1  # Number of times to repeat the benchmark
RUN_ALL=false  # Run benchmarks on all available queue implementations 

# Usage function
usage() {
    echo ""
    echo "SPMC Queue Micro Benchmark Runner"
    echo "=================================="
    echo ""
    echo -e "${BLUE}Usage:${NC} $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -o OPS        Operations per consumer (default: 10000)"
    echo "  -p PROCS      Total number of MPI processes (default: 5 = 1 producer + 4 consumers)"
    echo "  -H HOSTS      Comma-separated list of MPI hosts/nodes"
    echo "                Example: -H node1,node2,node3"
    echo "  -s PATH       Path to SPMC implementation directory"
    echo "                (if not specified, will auto-detect or prompt)"
    echo "  -a            Run benchmarks on ALL available queue implementations"
    echo "  -t SECONDS    Timeout for each benchmark run (default: 30 seconds)"
    echo "                Note: Applies to each individual run, not total time"
    echo "  -r REPEAT     Number of times to repeat the benchmark (default: 1)"
    echo "  -v            Enable verbose output"
    echo "  -h            Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                                  # Auto-detect queue, 5 processes (1+4), 10K ops"
    echo "  $0 -a                               # Run all available queues"
    echo "  $0 -p 9                             # Run with 9 processes (1 producer + 8 consumers)"
    echo "  $0 -p 5 -s ../spmc_BBQ              # Specify BBQ implementation"
    echo "  $0 -p 9 -o 50000                    # 8 consumers, 50K ops per consumer"
    echo "  $0 -p 5 -H node1,node2              # Use 5 processes across 2 nodes"
    echo "  $0 -p 9 -s ../spmc_dFFQ -v          # Verbose output with dFFQ"
    echo "  $0 -r 5 -p 5 -o 10000               # Repeat benchmark 5 times"
    echo "  $0 -a -p 9 -r 3                     # Run all queues, 8 consumers, 3 times each"
    echo ""
    echo "Note: Process count = 1 producer + N consumers"
    echo "      Example: -p 5 means 1 producer + 4 consumers"
    echo ""
    exit 0
}

# Parse command line arguments
while getopts "o:p:H:s:t:r:avh" opt; do
    case $opt in
        o) OPS_PER_CONSUMER=$OPTARG ;;
        p) NUM_PROCESSES=$OPTARG ;;
        H) MPI_HOSTS=$OPTARG ;;
        s) SPMC_PATH=$OPTARG ;;
        t) TIMEOUT=$OPTARG ;;
        r) REPEAT=$OPTARG ;;
        a) RUN_ALL=true ;;
        v) VERBOSE=true ;;
        h) usage ;;
        *) usage ;;
    esac
done

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

# Function to detect available SPMC implementations
detect_spmc_types() {
    local spmc_dirs=()
    
    # Search for spmc_* directories in workspace
    for dir in "${WORKSPACE_DIR}"/spmc_*/; do
        if [[ -d "$dir" && -f "$dir/Makefile" ]]; then
            local dirname=$(basename "$dir")
            spmc_dirs+=("$dirname")
        fi
    done
    
    # Also check for spmc* pattern (without underscore)
    for dir in "${WORKSPACE_DIR}"/spmc*/; do
        if [[ -d "$dir" && -f "$dir/Makefile" ]]; then
            local dirname=$(basename "$dir")
            # Avoid duplicates
            if [[ ! " ${spmc_dirs[@]} " =~ " ${dirname} " ]]; then
                spmc_dirs+=("$dirname")
            fi
        fi
    done
    
    echo "${spmc_dirs[@]}"
}

# Function to select SPMC implementation
select_spmc_implementation() {
    # If user specified a path, use it
    if [[ -n "$SPMC_PATH" ]]; then
        # Handle different path formats
        local resolved_path=""
        
        if [[ "$SPMC_PATH" = /* ]]; then
            # Absolute path
            resolved_path="$SPMC_PATH"
        elif [[ "$SPMC_PATH" = ../* ]] || [[ "$SPMC_PATH" = ./* ]]; then
            # Relative path from benchmark directory
            resolved_path="$(cd "${BENCHMARK_DIR}" && cd "$SPMC_PATH" && pwd)"
        else
            # Try as relative to workspace
            resolved_path="${WORKSPACE_DIR}/$SPMC_PATH"
        fi
        
        if [[ ! -d "$resolved_path" ]]; then
            log_error "Specified SPMC path '$SPMC_PATH' does not exist"
            log_error "Resolved to: $resolved_path"
            exit 1
        fi
        
        local spmc_name=$(basename "$resolved_path")
        log_info "Using specified SPMC implementation: $spmc_name"
        echo "$resolved_path"
        return
    fi
    
    # Auto-detect available implementations
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

# Function to get all SPMC implementations
get_all_spmc_implementations() {
    local available_types=($(detect_spmc_types))
    
    if [[ ${#available_types[@]} -eq 0 ]]; then
        log_error "No SPMC implementations found in workspace"
        exit 1
    fi
    
    local spmc_paths=()
    for type in "${available_types[@]}"; do
        spmc_paths+=("${WORKSPACE_DIR}/${type}")
    done
    
    echo "${spmc_paths[@]}"
}

# Check for conflicting options
if [[ "$RUN_ALL" = true && -n "$SPMC_PATH" ]]; then
    log_error "Cannot use both -a (run all) and -s (specify path) options together"
    exit 1
fi

# Detect and select SPMC implementation(s)
if [[ "$RUN_ALL" = true ]]; then
    SPMC_IMPL_PATHS=($(get_all_spmc_implementations))
    log_info "Running benchmarks on ${#SPMC_IMPL_PATHS[@]} queue implementations"
else
    SPMC_IMPL_PATH=$(select_spmc_implementation)
    SPMC_IMPL_PATHS=("$SPMC_IMPL_PATH")
fi

NUM_CONSUMERS=$((NUM_PROCESSES - 1))

# Create results directory
mkdir -p "${SESSION_DIR}"

echo -e "${GREEN}======================================${NC}"
echo -e "${GREEN}SPMC Queue Micro Benchmark${NC}"
echo -e "${GREEN}======================================${NC}"
echo ""
echo -e "${BLUE}Configuration:${NC}"
if [[ "$RUN_ALL" = true ]]; then
    echo "  Queue Implementations:    ${#SPMC_IMPL_PATHS[@]} queues (run all)"
else
    echo "  SPMC Implementation:      $(basename "${SPMC_IMPL_PATHS[0]}")"
fi
echo "  Operations per consumer:  ${OPS_PER_CONSUMER}"
echo "  Total processes:          ${NUM_PROCESSES} (1 producer + ${NUM_CONSUMERS} consumers)"
echo "  Repeat count:             ${REPEAT}"
echo "  Timeout per run:          ${TIMEOUT} seconds"
if [ ! -z "${MPI_HOSTS}" ]; then
    echo "  MPI hosts:                ${MPI_HOSTS}"
fi
if [ "$VERBOSE" = true ]; then
    echo "  Verbose output:           Enabled"
fi
echo "  Results directory:        ${SESSION_DIR}"
echo ""

# Log file
LOG_FILE="${SESSION_DIR}/benchmark_log.txt"
echo "Micro Benchmark Session: ${TIMESTAMP}" > "${LOG_FILE}"
if [[ "$RUN_ALL" = true ]]; then
    echo "Running ALL queue implementations (${#SPMC_IMPL_PATHS[@]} total)" >> "${LOG_FILE}"
    for impl_path in "${SPMC_IMPL_PATHS[@]}"; do
        echo "  - $(basename "$impl_path")" >> "${LOG_FILE}"
    done
else
    echo "SPMC Implementation: $(basename "${SPMC_IMPL_PATHS[0]}")" >> "${LOG_FILE}"
fi
echo "Operations per consumer: ${OPS_PER_CONSUMER}" >> "${LOG_FILE}"
echo "Total processes: ${NUM_PROCESSES} (1 producer + ${NUM_CONSUMERS} consumers)" >> "${LOG_FILE}"
echo "Repeat count: ${REPEAT}" >> "${LOG_FILE}"
if [ ! -z "${MPI_HOSTS}" ]; then
    echo "MPI hosts: ${MPI_HOSTS}" >> "${LOG_FILE}"
fi
echo "" >> "${LOG_FILE}"

# Function to build SPMC implementation executable for microbenchmark
build_microbenchmark_executable() {
    local spmc_path="$1"
    local spmc_name=$(basename "$spmc_path")
    
    log_info "Building microbenchmark for SPMC implementation: $spmc_name"
    
    # First, build the microbenchmark library itself
    log_info "Building microbenchmark library..."
    cd "$BENCHMARK_DIR" || {
        log_error "Failed to change directory to $BENCHMARK_DIR"
        return 1
    }
    
    if ! make clean > /dev/null 2>&1; then
        log_warning "Clean failed, continuing anyway..."
    fi
    
    if ! make all > /dev/null 2>&1; then
        log_error "Failed to build microbenchmark library"
        log_error "Run 'make all' manually in microbenchmark directory to see errors"
        return 1
    fi
    
    log_success "Microbenchmark library built successfully"
    
    # Now ensure the SPMC implementation itself is built
    if [[ ! -f "$spmc_path/Makefile" ]]; then
        log_error "No Makefile found in $spmc_path"
        return 1
    fi
    
    # Build the SPMC library/objects
    cd "$spmc_path" || {
        log_error "Failed to change directory to $spmc_path"
        return 1
    }
    
    make clean > /dev/null 2>&1
    if ! make all > /dev/null 2>&1; then
        log_error "Failed to build SPMC implementation"
        log_error "Run 'make all' manually in $spmc_path to see errors"
        cd "$BENCHMARK_DIR"
        return 1
    fi
    
    cd "$BENCHMARK_DIR"
    
    log_info "Preparing to build microbenchmark executable..."
    
    # Now build the microbenchmark executable
    local benchmark_obj="${BENCHMARK_DIR}/examples/spmc_microbenchmark.o"
    local exe_out="${BENCHMARK_DIR}/bin/microbenchmark_${spmc_name}"
    
    # Create bin directory if needed
    mkdir -p "${BENCHMARK_DIR}/bin"
    
    log_info "Output executable: $exe_out"
    
    # Find SPMC library or object files
    local spmc_libs=""
    if [[ -f "${spmc_path}/build/spmc_queue.o" && -f "${spmc_path}/build/bitmap.o" ]]; then
        spmc_libs="${spmc_path}/build/spmc_queue.o ${spmc_path}/build/bitmap.o"
        log_info "Using object files: ${spmc_libs}"
    elif [[ -f "${spmc_path}/build/spmc_queue.o" ]]; then
        spmc_libs="${spmc_path}/build/spmc_queue.o"
        log_info "Using single object file: ${spmc_libs}"
    elif [[ -f "${spmc_path}/spmc_queue.o" ]]; then
        spmc_libs="${spmc_path}/spmc_queue.o"
        log_info "Using single object file: ${spmc_libs}"
    else
        log_error "SPMC object files not found in: ${spmc_path}"
        return 1
    fi
    
    # Compile the microbenchmark source
    log_info "Compiling microbenchmark source..."
    if ! mpicc -c "${BENCHMARK_DIR}/examples/spmc_microbenchmark.c" \
        -I"$spmc_path" \
        -I"$spmc_path/src" \
        -I"${WORKSPACE_DIR}/mpi_lib/include" \
        -o "$benchmark_obj" 2>&1; then
        log_error "Failed to compile microbenchmark source"
        return 1
    fi
    
    log_success "Compiled microbenchmark object: $benchmark_obj"
    
    # Link the executable - use static library directly
    local micro_bench_lib="${BENCHMARK_DIR}/lib/libmicrobenchmark.a"
    local mpi_wrapper_lib="${WORKSPACE_DIR}/mpi_lib/lib/libmpi_wrapper.a"
    
    if [[ ! -f "$micro_bench_lib" ]]; then
        log_error "Microbenchmark library not found: $micro_bench_lib"
        return 1
    fi
    
    if [[ ! -f "$mpi_wrapper_lib" ]]; then
        log_error "MPI wrapper library not found: $mpi_wrapper_lib"
        return 1
    fi
    
    log_info "Linking microbenchmark executable..."
    if ! mpicc "$benchmark_obj" $spmc_libs \
        "$micro_bench_lib" "$mpi_wrapper_lib" \
        -o "$exe_out" \
        -I"$spmc_path" \
        -I"$spmc_path/src" \
        -I"${WORKSPACE_DIR}/mpi_lib/include" \
        -lmpi -lm 2>&1; then
        log_error "Failed to link microbenchmark executable"
        return 1
    fi
    
    # Set executable permissions (important for cluster with shared filesystem)
    chmod +x "$exe_out"
    
    # Verify permissions were set correctly
    if [ ! -x "$exe_out" ]; then
        log_error "Failed to set executable permission on: $exe_out"
        log_error "Current permissions: $(ls -l "$exe_out")"
        return 1
    fi
    
    log_success "Microbenchmark executable built successfully: $exe_out"
    log_info "Permissions: $(ls -l "$exe_out")"
    
    echo "$exe_out"
    return 0
}

# Function to sync executable to all MPI hosts
sync_executable_to_hosts() {
    local executable="$1"
    
    if [[ -z "$MPI_HOSTS" ]]; then
        return 0
    fi
    
    log_info "Synchronizing executable to all MPI hosts: $MPI_HOSTS"
    
    # Convert comma-separated hosts to array
    IFS=',' read -ra HOST_ARRAY <<< "$MPI_HOSTS"
    
    for host in "${HOST_ARRAY[@]}"; do
        # Skip synchronizing to localhost/current host
        if [[ "$host" != "$(hostname)" && "$host" != "localhost" && "$host" != "127.0.0.1" ]]; then
            log_info "Syncing executable to $host..."
            
            # First create the directory on remote host
            if ssh -o ConnectTimeout=10 -o BatchMode=yes "$host" "mkdir -p '$(dirname "$executable")'" 2>/dev/null; then
                # Then sync the executable
                if rsync -aqz "$executable" "${host}:${executable}" 2>/dev/null; then
                    log_success "Successfully synced executable to $host"
                    # Set executable permissions on remote host
                    ssh -o ConnectTimeout=10 -o BatchMode=yes "$host" "chmod +x '$executable'" 2>/dev/null
                else
                    log_warning "rsync failed, trying scp..."
                    # Fallback: try scp
                    if scp -o ConnectTimeout=10 -o BatchMode=yes "$executable" "${host}:${executable}" 2>/dev/null; then
                        log_success "Successfully synced executable to $host via scp"
                        ssh -o ConnectTimeout=10 -o BatchMode=yes "$host" "chmod +x '$executable'" 2>/dev/null
                    else
                        log_error "Failed to sync executable to $host (both rsync and scp failed)"
                        log_warning "Benchmark may fail on $host"
                    fi
                fi
            else
                log_error "Failed to create directory on $host: $(dirname "$executable")"
            fi
        else
            log_info "Skipping sync to local host: $host"
        fi
    done
    
    log_success "Executable synchronization completed"
    return 0
}

# Function to run the microbenchmark
run_microbenchmark() {
    local executable="$1"
    local num_procs="$2"
    local ops="$3"
    local spmc_name="$4"
    local run_number="${5:-1}"
    
    local num_consumers=$((num_procs - 1))
    local output_file="${SESSION_DIR}/microbench_${spmc_name}_${num_consumers}consumers_${ops}ops_run${run_number}.csv"
    
    log_info "Running microbenchmark (run ${run_number}/${REPEAT}): ${spmc_name} with ${num_consumers} consumers (${num_procs} processes)"
    
    # Build MPI command with timeout
    local mpi_cmd="timeout ${TIMEOUT}s mpirun -np ${num_procs}"
    
    # Add host specification if provided
    if [ ! -z "${MPI_HOSTS}" ]; then
        local mpi_version=$(mpirun --version 2>&1 | head -1)
        
        if echo "$mpi_version" | grep -qi "open.mpi\|openmpi"; then
            mpi_cmd="$mpi_cmd -H ${MPI_HOSTS}"
            if [[ -n "${WSL_DISTRO_NAME}" ]] || [[ "$EUID" -eq 0 ]]; then
                mpi_cmd="$mpi_cmd --allow-run-as-root"
            fi
        elif echo "$mpi_version" | grep -qi "mpich\|hydra"; then
            mpi_cmd="$mpi_cmd -hosts ${MPI_HOSTS}"
        else
            mpi_cmd="$mpi_cmd -H ${MPI_HOSTS}"
        fi
        
        if [ "$VERBOSE" = true ]; then
            log_info "MPI command: ${mpi_cmd}"
        fi
    fi
    
    # Ensure executable has correct permissions (important for cluster environments)
    chmod +x "${executable}"
    
    # Run the benchmark
    local start_time=$(date +%s)
    
    if $VERBOSE; then
        log_info "Running: ${mpi_cmd} ${executable} ${ops}"
    fi
    
    # Run and capture exit code
    ${mpi_cmd} "${executable}" ${ops} > "${SESSION_DIR}/microbench_stdout_run${run_number}.txt" 2>&1
    local exit_code=$?
    
    local end_time=$(date +%s)
    local duration=$((end_time - start_time))
    
    # Check for timeout (exit code 124 from timeout command)
    if [ $exit_code -eq 124 ]; then
        log_error "Benchmark timed out after ${TIMEOUT} seconds"
        echo "TIMEOUT [Run ${run_number}]: ${spmc_name} (${num_consumers} consumers, ${ops} ops) - ${TIMEOUT}s" >> "${LOG_FILE}"
        return 124
    fi
    
    # Check for other errors
    if [ $exit_code -ne 0 ]; then
        log_error "Benchmark failed with exit code: ${exit_code}"
        echo "FAILED [Run ${run_number}]: ${spmc_name} (${num_consumers} consumers, ${ops} ops) - exit code ${exit_code}" >> "${LOG_FILE}"
        cat "${SESSION_DIR}/microbench_stdout_run${run_number}.txt" | tail -20
        return $exit_code
    fi
    
    log_success "Completed in ${duration}s"
    echo "SUCCESS [Run ${run_number}]: ${spmc_name} (${num_consumers} consumers, ${ops} ops) - ${duration}s" >> "${LOG_FILE}"
    
    # Look for generated CSV and rename it
    local csv_file="microbench_spmc_${num_procs}procs_${ops}ops.csv"
    if [ -f "${csv_file}" ]; then
        mv "${csv_file}" "${output_file}"
        log_success "Results saved to: ${output_file}"
    elif [ -f "${SESSION_DIR}/${csv_file}" ]; then
        mv "${SESSION_DIR}/${csv_file}" "${output_file}"
        log_success "Results saved to: ${output_file}"
    else
        log_warning "CSV file not found, generating from stdout..."
        generate_csv_from_stdout "${SESSION_DIR}/microbench_stdout_run${run_number}.txt" "${output_file}" "${spmc_name}" ${num_procs} ${ops}
    fi
    
    # Append to queue-specific enhanced summary CSV (consolidated across all runs)
    local enhanced_summary="${SESSION_DIR}/enhanced_summary_${spmc_name}.csv"
    local temp_csv="${SESSION_DIR}/temp_run${run_number}.csv"
    
    # Generate CSV for this run
    generate_csv_from_stdout "${SESSION_DIR}/microbench_stdout_run${run_number}.txt" "${temp_csv}" "${spmc_name}" ${num_procs} ${ops}
    
    # If this is the first run, copy header and data
    if [ ${run_number} -eq 1 ]; then
        cp "${temp_csv}" "${enhanced_summary}"
        log_success "Created enhanced summary: ${enhanced_summary}"
    else
        # For subsequent runs, append only the data row (skip header)
        tail -n +2 "${temp_csv}" >> "${enhanced_summary}"
        log_success "Appended run ${run_number} to enhanced summary"
    fi
    
    # Clean up temp file
    rm -f "${temp_csv}"
    
    return 0
}

# Function to display results for a single queue
display_queue_results() {
    local spmc_name="$1"
    local csv_file="${SESSION_DIR}/enhanced_summary_${spmc_name}.csv"
    
    if [ ! -f "${csv_file}" ]; then
        return
    fi
    
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}Results Summary - ${spmc_name}${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo ""
    
    echo "SPMC Implementation:     ${spmc_name}"
    echo "Number of Consumers:     ${NUM_CONSUMERS}"
    echo "Operations per Consumer: ${OPS_PER_CONSUMER}"
    echo "Timeout per run:         ${TIMEOUT} seconds"
    echo "Successful Runs:         ${SUCCESS_COUNT}/${REPEAT}"
    echo ""
    
    if [ $REPEAT -gt 1 ]; then
        # Calculate statistics across multiple runs
        log_info "Calculating statistics across ${SUCCESS_COUNT} successful runs..."
        
        # Extract metrics from CSV (skip header)
        producer_times=$(tail -n +2 "$csv_file" | cut -d',' -f7)
        enqueue_throughputs=$(tail -n +2 "$csv_file" | cut -d',' -f8)
        avg_consumer_times=$(tail -n +2 "$csv_file" | cut -d',' -f9)
        dequeue_throughputs=$(tail -n +2 "$csv_file" | cut -d',' -f12)
        latencies=$(tail -n +2 "$csv_file" | cut -d',' -f13)
        
        # Calculate mean and std dev for each metric
        calc_stats() {
            local values="$1"
            
            # Filter out empty lines and keep valid numbers
            values=$(echo "$values" | grep -v '^$' | grep -E '^\.?[0-9]+\.?[0-9]*$')
            
            if [ -z "$values" ]; then
                echo "0.0 0.0"
                return
            fi
            
            local stats=$(echo "$values" | awk '
            {
                sum += $1
                values[NR] = $1
                count = NR
            }
            END {
                if (count == 0) {
                    print "0.0 0.0"
                } else {
                    mean = sum / count
                    variance = 0
                    for (i = 1; i <= count; i++) {
                        diff = values[i] - mean
                        variance += diff * diff
                    }
                    variance = variance / count
                    stddev = sqrt(variance)
                    printf "%.6f %.6f", mean, stddev
                }
            }')
            
            echo "$stats"
        }
        
        read producer_mean producer_std <<< $(calc_stats "$producer_times")
        read enqueue_mean enqueue_std <<< $(calc_stats "$enqueue_throughputs")
        read consumer_mean consumer_std <<< $(calc_stats "$avg_consumer_times")
        read dequeue_mean dequeue_std <<< $(calc_stats "$dequeue_throughputs")
        read latency_mean latency_std <<< $(calc_stats "$latencies")
        
        echo "Producer Time (sec):"
        printf "  Mean:   %.6f ± %.6f\n" $producer_mean $producer_std
        echo ""
        echo "Enqueue Throughput (ops/sec):"
        printf "  Mean:   %.2f ± %.2f\n" $enqueue_mean $enqueue_std
        echo ""
        echo "Avg Consumer Time (sec):"
        printf "  Mean:   %.6f ± %.6f\n" $consumer_mean $consumer_std
        echo ""
        echo "Dequeue Throughput (ops/sec):"
        printf "  Mean:   %.2f ± %.2f\n" $dequeue_mean $dequeue_std
        echo ""
        echo "Avg Latency (ms):"
        printf "  Mean:   %.6f ± %.6f\n" $latency_mean $latency_std
        echo ""
    else
        # Single run - show individual metrics
        producer_time=$(tail -n +2 "${csv_file}" | cut -d',' -f7)
        enqueue_throughput=$(tail -n +2 "${csv_file}" | cut -d',' -f8)
        avg_consumer_time=$(tail -n +2 "${csv_file}" | cut -d',' -f9)
        dequeue_throughput=$(tail -n +2 "${csv_file}" | cut -d',' -f12)
        avg_latency=$(tail -n +2 "${csv_file}" | cut -d',' -f13)
        
        if [ ! -z "${enqueue_throughput}" ] && [ "${enqueue_throughput}" != "N/A" ]; then
            printf "Enqueue Throughput:     %.2f ops/sec\n" ${enqueue_throughput}
        fi
        if [ ! -z "${dequeue_throughput}" ] && [ "${dequeue_throughput}" != "N/A" ]; then
            printf "Dequeue Throughput:     %.2f ops/sec\n" ${dequeue_throughput}
        fi
        if [ ! -z "${avg_consumer_time}" ] && [ "${avg_consumer_time}" != "N/A" ]; then
            printf "Avg Consumer Time:      %.6f sec\n" ${avg_consumer_time}
        fi
        if [ ! -z "${avg_latency}" ] && [ "${avg_latency}" != "N/A" ]; then
            printf "Avg Latency:            %.6f ms\n" ${avg_latency}
        fi
        if [ ! -z "${producer_time}" ] && [ "${producer_time}" != "N/A" ]; then
            printf "Producer Phase Time:    %.6f sec\n" ${producer_time}
        fi
    fi
    
    echo ""
    echo -e "${BLUE}Results File:${NC} ${csv_file}"
}

# Function to generate comparison summary across all queues
generate_comparison_summary() {
    local comparison_file="${SESSION_DIR}/all_queues_comparison.csv"
    local consolidated_file="${SESSION_DIR}/all_queues_consolidated.csv"
    
    # First, consolidate all queue results into one file
    local first_file=true
    for impl_path in "${SPMC_IMPL_PATHS[@]}"; do
        local queue_name=$(basename "$impl_path")
        
        # Look for queue-specific enhanced summary
        local queue_summary="${SESSION_DIR}/enhanced_summary_${queue_name}.csv"
        
        if [ -f "$queue_summary" ]; then
            if [ "$first_file" = true ]; then
                # Copy header and data from first file
                cat "$queue_summary" > "$consolidated_file"
                first_file=false
            else
                # Append only data (skip header)
                tail -n +2 "$queue_summary" >> "$consolidated_file"
            fi
        else
            log_warning "No enhanced summary found for ${queue_name}: ${queue_summary}"
        fi
    done
    
    if [ ! -f "$consolidated_file" ] || [ ! -s "$consolidated_file" ]; then
        log_warning "No queue results found for comparison"
        return
    fi
    
    # Create comparison summary header
    echo "Queue_Implementation,Num_Consumers,Ops_Per_Consumer,Avg_Enqueue_Throughput,Avg_Dequeue_Throughput,Avg_Latency_Ms,Total_Runs" > "$comparison_file"
    
    # Collect data from each queue
    local queues_found=0
    for impl_path in "${SPMC_IMPL_PATHS[@]}"; do
        local queue_name=$(basename "$impl_path")
        
        # Extract rows for this queue from consolidated file
        local queue_data=$(tail -n +2 "$consolidated_file" | grep "\"${queue_name}\"" || true)
        
        if [ -z "$queue_data" ]; then
            log_warning "No data found for ${queue_name} in consolidated file"
            continue
        fi
        
        queues_found=$((queues_found + 1))
        
        # Calculate averages across all runs for this queue
        local enqueue_avg=$(echo "$queue_data" | cut -d',' -f8 | awk '{sum+=$1; count++} END {if(count>0) printf "%.2f", sum/count; else print "0.0"}')
        local dequeue_avg=$(echo "$queue_data" | cut -d',' -f12 | awk '{sum+=$1; count++} END {if(count>0) printf "%.2f", sum/count; else print "0.0"}')
        local latency_avg=$(echo "$queue_data" | cut -d',' -f13 | awk '{sum+=$1; count++} END {if(count>0) printf "%.6f", sum/count; else print "0.0"}')
        local run_count=$(echo "$queue_data" | wc -l)
        
        echo "\"${queue_name}\",${NUM_CONSUMERS},${OPS_PER_CONSUMER},${enqueue_avg},${dequeue_avg},${latency_avg},${run_count}" >> "$comparison_file"
    done
    
    if [ $queues_found -eq 0 ]; then
        log_error "No queue data found in consolidated file"
        return
    fi
    
    log_success "Consolidated results: ${consolidated_file}"
    log_success "Comparison summary: ${comparison_file}"
    
    # Display comparison table
    echo ""
    echo -e "${BLUE}Performance Comparison:${NC}"
    echo ""
    
    # Use printf for better formatting if column is not available
    if command -v column &> /dev/null; then
        column -t -s',' "$comparison_file" | sed 's/"//g'
    else
        # Fallback: simple display without column
        cat "$comparison_file" | sed 's/"//g'
    fi
    echo ""
}

# Function to generate enhanced CSV from micro benchmark output
generate_csv_from_stdout() {
    local stdout_file="$1"
    local output_csv="$2"
    local queue_name="$3"
    local num_procs="$4"
    local ops_per_consumer="$5"
    
    local num_consumers=$((num_procs - 1))
    
    # Extract metrics from stdout
    local producer_time=$(grep "Rank 0: Completed" "$stdout_file" | grep -oP '\d+\.\d+(?= seconds)' | head -1)
    local producer_throughput=$(grep "Rank 0: Completed" "$stdout_file" | grep -oP '\d+\.\d+(?= ops/sec)' | head -1)
    local producer_ops=$(grep "Rank 0: Completed" "$stdout_file" | grep -oP 'Completed \K\d+' | head -1)
    
    # Extract consumer metrics (ranks 1+)
    local consumer_times=$(grep -P "Rank [1-9]\d*: Completed" "$stdout_file" | grep -oP '\d+\.\d+(?= seconds)')
    local consumer_throughputs=$(grep -P "Rank [1-9]\d*: Completed" "$stdout_file" | grep -oP '\d+\.\d+(?= ops/sec)')
    local consumer_ops=$(grep -P "Rank [1-9]\d*: Completed" "$stdout_file" | grep -oP 'Completed \K\d+' | head -1)
    
    # Calculate statistics
    local total_consumer_throughput=0
    local min_time=999999
    local max_time=0
    local sum_time=0
    local count=0
    
    while IFS= read -r time; do
        sum_time=$(echo "$sum_time + $time" | bc)
        count=$((count + 1))
        if (( $(echo "$time < $min_time" | bc -l) )); then
            min_time=$time
        fi
        if (( $(echo "$time > $max_time" | bc -l) )); then
            max_time=$time
        fi
    done <<< "$consumer_times"
    
    # Calculate average consumer time first
    local avg_consumer_time=$(echo "scale=6; $sum_time / $count" | bc)
    
    # Calculate per-consumer throughput from consumer time (ensures consistency)
    # Per-consumer throughput = ops_per_consumer / avg_consumer_time
    local avg_consumer_throughput=$(echo "scale=2; $ops_per_consumer / $avg_consumer_time" | bc)
    
    # Calculate latency per operation (in milliseconds)
    local avg_latency_ms=$(echo "scale=6; ($avg_consumer_time / $ops_per_consumer) * 1000" | bc)
    
    # Calculate total operations in measurement phase (2A + 2B)
    # Phase 2A: Producer enqueues 2N items (2 * num_consumers * ops_per_consumer)
    # Phase 2B: Consumers dequeue N items (num_consumers * ops_per_consumer)
    # Total = 2N + N = 3N items
    local total_ops=$((3 * num_consumers * ops_per_consumer))
    
    # Get memory usage - extract from "Queue Memory Usage: 7.63 MB (8000000 bytes)"
    local memory_mb=$(grep -oP 'Queue Memory Usage:\s*\K[\d.]+' "$stdout_file" | head -1)
    if [ -z "$memory_mb" ]; then
        memory_mb="0.0"
    fi
    
    # Write CSV in format similar to benchmark.c
    {
        echo "Test_Name,Queue_Implementation,MPI_Size,Num_Consumers,Ops_Per_Consumer,Total_Operations,Producer_Time_Sec,Enqueue_Throughput_Ops_Per_Sec,Avg_Consumer_Time_Sec,Min_Consumer_Time_Sec,Max_Consumer_Time_Sec,Dequeue_Throughput_Ops_Per_Sec,Avg_Latency_Ms,Memory_Usage_MB"
        echo "\"Micro Benchmark\",\"${queue_name}\",${num_procs},${num_consumers},${ops_per_consumer},${total_ops},${producer_time:-0.0},${producer_throughput:-0.0},${avg_consumer_time:-0.0},${min_time},${max_time},${avg_consumer_throughput:-0.0},${avg_latency_ms:-0.0},${memory_mb:-0.0}"
    } > "$output_csv"
    
    log_success "Generated CSV from stdout: $output_csv"
}

# Function to generate enhanced summary CSV compatible with benchmark.c format
generate_enhanced_csv() {
    local source_csv="$1"
    local output_csv="$2"
    local queue_name="$3"
    local num_procs="$4"
    local ops_per_consumer="$5"
    local total_duration="$6"
    
    local num_consumers=$((num_procs - 1))
    
    if [ ! -f "$source_csv" ]; then
        log_warning "Source CSV not found, cannot generate enhanced summary"
        return 1
    fi
    
    # Check if source CSV is already in the enhanced format
    if head -1 "$source_csv" | grep -q "Test_Name.*Queue_Implementation"; then
        # Source is already in enhanced format, just copy it
        cp "$source_csv" "$output_csv"
        log_success "Enhanced summary already exists: $output_csv"
        return 0
    fi
    
    # Extract metrics from simple CSV format
    local producer_time=$(grep -i "Producer.*Time" "$source_csv" | cut -d',' -f2 | tr -d ' ')
    local enqueue_throughput=$(grep -i "Producer.*Throughput" "$source_csv" | cut -d',' -f2 | tr -d ' ')
    local avg_consumer_time=$(grep -i "Avg.*Consumer.*Time\|Average.*Consumer.*Time" "$source_csv" | cut -d',' -f2 | tr -d ' ')
    local min_consumer_time=$(grep -i "Min.*Consumer.*Time" "$source_csv" | cut -d',' -f2 | tr -d ' ')
    local max_consumer_time=$(grep -i "Max.*Consumer.*Time" "$source_csv" | cut -d',' -f2 | tr -d ' ')
    
    # Calculate average consumer (dequeue) throughput, not total
    local dequeue_throughput=$(echo "$ops_per_consumer $avg_consumer_time" | awk '{if($2>0) printf "%.2f", $1/$2; else print "0.0"}')
    
    # Calculate latency per operation
    local avg_latency_ms=$(echo "$avg_consumer_time $ops_per_consumer" | awk '{if($2>0) printf "%.6f", ($1/$2)*1000; else print "0.0"}')
    
    # Calculate total operations (3N = 2N enqueue + N dequeue)
    local total_ops=$((3 * num_consumers * ops_per_consumer))
    
    # Get memory usage - only match CSV data line, not comments
    local memory_mb=$(grep -i "^Memory Usage," "$source_csv" | cut -d',' -f2 | tr -d ' ')
    if [ -z "$memory_mb" ] || [ "$memory_mb" = "N/A" ]; then
        memory_mb="0.0"
    fi
    
    # Create enhanced CSV matching benchmark.c format
    {
        echo "Test_Name,Queue_Implementation,MPI_Size,Num_Consumers,Ops_Per_Consumer,Total_Operations,Producer_Time_Sec,Enqueue_Throughput_Ops_Per_Sec,Avg_Consumer_Time_Sec,Min_Consumer_Time_Sec,Max_Consumer_Time_Sec,Dequeue_Throughput_Ops_Per_Sec,Avg_Latency_Ms,Memory_Usage_MB"
        echo "\"Micro Benchmark\",\"${queue_name}\",${num_procs},${num_consumers},${ops_per_consumer},${total_ops},${producer_time:-0.0},${enqueue_throughput:-0.0},${avg_consumer_time:-0.0},${min_consumer_time:-0.0},${max_consumer_time:-0.0},${dequeue_throughput:-0.0},${avg_latency_ms},${memory_mb}"
    } > "$output_csv"
    
    log_success "Generated enhanced summary: $output_csv"
}

# Main execution
TOTAL_SUCCESS_COUNT=0
TOTAL_FAILED_COUNT=0
TOTAL_QUEUE_COUNT=${#SPMC_IMPL_PATHS[@]}

# Loop through all queue implementations
for queue_idx in "${!SPMC_IMPL_PATHS[@]}"; do
    SPMC_IMPL_PATH="${SPMC_IMPL_PATHS[$queue_idx]}"
    SPMC_NAME=$(basename "$SPMC_IMPL_PATH")
    
    if [[ "$RUN_ALL" = true ]]; then
        echo ""
        echo -e "${GREEN}======================================${NC}"
        echo -e "${GREEN}Queue $((queue_idx + 1))/${TOTAL_QUEUE_COUNT}: ${SPMC_NAME}${NC}"
        echo -e "${GREEN}======================================${NC}"
        echo ""
    fi
    
    log_info "Building microbenchmark executable for ${SPMC_NAME}..."
    EXECUTABLE=$(build_microbenchmark_executable "$SPMC_IMPL_PATH")
    
    if [[ $? -ne 0 ]] || [[ -z "$EXECUTABLE" ]]; then
        log_error "Failed to build microbenchmark executable for ${SPMC_NAME}"
        log_warning "Skipping ${SPMC_NAME}..."
        echo "BUILD_FAILED: ${SPMC_NAME}" >> "${LOG_FILE}"
        continue
    fi
    
    # Sync executable to all MPI hosts if running on cluster
    if [[ -n "$MPI_HOSTS" ]]; then
        echo ""
        sync_executable_to_hosts "$EXECUTABLE"
    fi
    
    echo ""
    log_info "Starting microbenchmark execution for ${SPMC_NAME}..."
    echo ""
    
    # Run the microbenchmark multiple times
    SUCCESS_COUNT=0
    FAILED_COUNT=0
    
    for ((run=1; run<=REPEAT; run++)); do
        if [ $REPEAT -gt 1 ]; then
            echo ""
            log_info "========== ${SPMC_NAME} - Run ${run}/${REPEAT} =========="
            echo ""
        fi
        
        if run_microbenchmark "$EXECUTABLE" "$NUM_PROCESSES" "$OPS_PER_CONSUMER" "$SPMC_NAME" "$run"; then
            SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
        else
            FAILED_COUNT=$((FAILED_COUNT + 1))
            log_warning "Run ${run} failed, continuing with remaining runs..."
        fi
    done
    
    # Update totals
    TOTAL_SUCCESS_COUNT=$((TOTAL_SUCCESS_COUNT + SUCCESS_COUNT))
    TOTAL_FAILED_COUNT=$((TOTAL_FAILED_COUNT + FAILED_COUNT))
    
    # Check if all runs completed successfully for this queue
    if [ $SUCCESS_COUNT -eq $REPEAT ]; then
        echo ""
        log_success "${SPMC_NAME}: All ${REPEAT} run(s) completed successfully!"
    elif [ $SUCCESS_COUNT -gt 0 ]; then
        echo ""
        log_warning "${SPMC_NAME}: ${SUCCESS_COUNT} out of ${REPEAT} runs completed successfully (${FAILED_COUNT} failed)"
    else
        log_error "${SPMC_NAME}: All runs failed"
    fi
    
    # Display results for this queue
    if [[ "$RUN_ALL" = false ]]; then
        # Only display detailed results if running single queue
        display_queue_results "$SPMC_NAME"
    fi
    
    echo ""
done

# Final summary for -a option
if [[ "$RUN_ALL" = true ]]; then
    echo ""
    echo -e "${GREEN}======================================${NC}"
    echo -e "${GREEN}All Queues Summary${NC}"
    echo -e "${GREEN}======================================${NC}"
    echo ""
    echo "Total queue implementations: ${TOTAL_QUEUE_COUNT}"
    echo "Total successful runs:       ${TOTAL_SUCCESS_COUNT}"
    echo "Total failed runs:           ${TOTAL_FAILED_COUNT}"
    echo ""
    
    # Display comparison table
    log_info "Generating comparison results..."
    generate_comparison_summary
fi

# Overall status
if [ $TOTAL_FAILED_COUNT -eq 0 ]; then
    echo ""
    log_success "All benchmarks completed successfully!"
    echo ""
elif [ $TOTAL_SUCCESS_COUNT -gt 0 ]; then
    echo ""
    log_warning "Some benchmarks failed (${TOTAL_SUCCESS_COUNT} succeeded, ${TOTAL_FAILED_COUNT} failed)"
    echo ""
else
    log_error "All benchmarks failed"
    exit 1
fi

# Display results - only show detailed results if not running all queues
# (for -a option, summary is shown in the main execution loop)
if [[ "$RUN_ALL" = false ]]; then
    SPMC_NAME=$(basename "${SPMC_IMPL_PATHS[0]}")
    csv_file="${SESSION_DIR}/enhanced_summary_${SPMC_NAME}.csv"
    
    if [ -f "${csv_file}" ]; then
        display_queue_results "$SPMC_NAME"
        
        # Show enhanced summary location
        echo -e "${BLUE}Enhanced Summary:${NC} ${csv_file}"
        if [ $REPEAT -gt 1 ]; then
            echo -e "${BLUE}  (Contains all ${SUCCESS_COUNT} runs)${NC}"
        fi
        
        # Show individual run details if multiple runs
        if [ $REPEAT -gt 1 ]; then
            echo ""
            echo -e "${BLUE}Individual Run Details:${NC}"
            for ((run=1; run<=REPEAT; run++)); do
                run_csv="${SESSION_DIR}/microbench_${SPMC_NAME}_${NUM_CONSUMERS}consumers_${OPS_PER_CONSUMER}ops_run${run}.csv"
                if [ -f "$run_csv" ]; then
                    echo "  Run ${run}: ${run_csv}"
                fi
            done
        fi
    fi
fi

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Benchmark Complete!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo -e "${BLUE}Results saved to:${NC} ${SESSION_DIR}"
echo -e "${BLUE}Detailed log:${NC} ${LOG_FILE}"
echo ""
