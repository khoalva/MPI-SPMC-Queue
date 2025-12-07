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
OPS_PER_CONSUMER=10000
NUM_PROCESSES=5  # Default: 1 producer + 4 consumers
MPI_HOSTS=""
SPMC_PATH=""
VERBOSE=false
TIMEOUT=20  # Default timeout: 300 seconds (5 minutes)

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
    echo "  -t SECONDS    Timeout for benchmark execution (default: 300 seconds)"
    echo "  -v            Enable verbose output"
    echo "  -h            Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                                  # Auto-detect queue, 5 processes (1+4)"
    echo "  $0 -p 9                             # Run with 9 processes (1 producer + 8 consumers)"
    echo "  $0 -p 5 -s ../spmc_BBQ              # Specify BBQ implementation"
    echo "  $0 -p 9 -o 20000                    # 8 consumers, 20K ops per consumer"
    echo "  $0 -p 5 -H node1,node2              # Use 5 processes across 2 nodes"
    echo "  $0 -p 9 -s ../spmc_dFFQ -v          # Verbose output with dFFQ"
    echo ""
    echo "Note: Process count = 1 producer + N consumers"
    echo "      Example: -p 5 means 1 producer + 4 consumers"
    echo ""
    exit 0
}

# Parse command line arguments
while getopts "o:p:H:s:t:vh" opt; do
    case $opt in
        o) OPS_PER_CONSUMER=$OPTARG ;;
        p) NUM_PROCESSES=$OPTARG ;;
        H) MPI_HOSTS=$OPTARG ;;
        s) SPMC_PATH=$OPTARG ;;
        t) TIMEOUT=$OPTARG ;;
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

# Detect and select SPMC implementation
SPMC_IMPL_PATH=$(select_spmc_implementation)
SPMC_NAME=$(basename "$SPMC_IMPL_PATH")
NUM_CONSUMERS=$((NUM_PROCESSES - 1))

# Create results directory
mkdir -p "${SESSION_DIR}"

echo -e "${GREEN}======================================${NC}"
echo -e "${GREEN}SPMC Queue Micro Benchmark${NC}"
echo -e "${GREEN}======================================${NC}"
echo ""
echo -e "${BLUE}Configuration:${NC}"
echo "  SPMC Implementation:      ${SPMC_NAME}"
echo "  Operations per consumer:  ${OPS_PER_CONSUMER}"
echo "  Total processes:          ${NUM_PROCESSES} (1 producer + ${NUM_CONSUMERS} consumers)"
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
echo "SPMC Implementation: ${SPMC_NAME}" >> "${LOG_FILE}"
echo "Operations per consumer: ${OPS_PER_CONSUMER}" >> "${LOG_FILE}"
echo "Total processes: ${NUM_PROCESSES} (1 producer + ${NUM_CONSUMERS} consumers)" >> "${LOG_FILE}"
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
    
    chmod +x "$exe_out"
    log_success "Microbenchmark executable built successfully: $exe_out"
    
    echo "$exe_out"
    return 0
}

# Function to run the microbenchmark
run_microbenchmark() {
    local executable="$1"
    local num_procs="$2"
    local ops="$3"
    local spmc_name="$4"
    
    local num_consumers=$((num_procs - 1))
    local output_file="${SESSION_DIR}/microbench_${spmc_name}_${num_consumers}consumers_${ops}ops.csv"
    
    log_info "Running microbenchmark: ${spmc_name} with ${num_consumers} consumers (${num_procs} processes)"
    
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
    
    # Run the benchmark
    local start_time=$(date +%s)
    
    if $VERBOSE; then
        log_info "Running: ${mpi_cmd} ${executable} ${ops}"
    fi
    
    # Run and capture exit code
    ${mpi_cmd} "${executable}" ${ops} > "${SESSION_DIR}/microbench_stdout.txt" 2>&1
    local exit_code=$?
    
    local end_time=$(date +%s)
    local duration=$((end_time - start_time))
    
    # Check for timeout (exit code 124 from timeout command)
    if [ $exit_code -eq 124 ]; then
        log_error "Benchmark timed out after ${TIMEOUT} seconds"
        echo "TIMEOUT: ${spmc_name} (${num_consumers} consumers, ${ops} ops) - ${TIMEOUT}s" >> "${LOG_FILE}"
        return 124
    fi
    
    # Check for other errors
    if [ $exit_code -ne 0 ]; then
        log_error "Benchmark failed with exit code: ${exit_code}"
        echo "FAILED: ${spmc_name} (${num_consumers} consumers, ${ops} ops) - exit code ${exit_code}" >> "${LOG_FILE}"
        cat "${SESSION_DIR}/microbench_stdout.txt" | tail -20
        return $exit_code
    fi
    
    log_success "Completed in ${duration}s"
    echo "SUCCESS: ${spmc_name} (${num_consumers} consumers, ${ops} ops) - ${duration}s" >> "${LOG_FILE}"
    
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
        generate_csv_from_stdout "${SESSION_DIR}/microbench_stdout.txt" "${output_file}" "${spmc_name}" ${num_procs} ${ops}
    fi
    
    # Generate enhanced summary CSV
    generate_enhanced_csv "${output_file}" "${SESSION_DIR}/enhanced_summary.csv" "${spmc_name}" ${num_procs} ${ops} ${duration}
    
    return 0
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
    
    while IFS= read -r throughput; do
        total_consumer_throughput=$(echo "$total_consumer_throughput + $throughput" | bc)
    done <<< "$consumer_throughputs"
    
    local avg_consumer_time=$(echo "scale=6; $sum_time / $count" | bc)
    local avg_latency_ms=$(echo "scale=6; ($avg_consumer_time / $consumer_ops) * 1000" | bc)
    
    # Get memory usage
    local memory_kb=$(grep -oP 'Memory.*:\s*\K\d+' "$stdout_file" | head -1)
    local memory_mb=$(echo "scale=2; ${memory_kb:-0} / 1024" | bc)
    
    # Write CSV in format similar to benchmark.c
    {
        echo "Test_Name,Queue_Implementation,MPI_Size,Num_Consumers,Ops_Per_Consumer,Total_Operations,Producer_Time_Sec,Producer_Throughput_Ops_Per_Sec,Avg_Consumer_Time_Sec,Min_Consumer_Time_Sec,Max_Consumer_Time_Sec,Total_Consumer_Throughput_Ops_Per_Sec,Avg_Latency_Ms,Memory_Usage_MB"
        echo "\"Micro Benchmark\",\"${queue_name}\",${num_procs},${num_consumers},${ops_per_consumer},$((num_consumers * consumer_ops)),${producer_time:-0.0},${producer_throughput:-0.0},${avg_consumer_time:-0.0},${min_time},${max_time},${total_consumer_throughput:-0.0},${avg_latency_ms:-0.0},${memory_mb:-0.0}"
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
    local producer_throughput=$(grep -i "Producer.*Throughput" "$source_csv" | cut -d',' -f2 | tr -d ' ')
    local avg_consumer_time=$(grep -i "Avg.*Consumer.*Time\|Average.*Consumer.*Time" "$source_csv" | cut -d',' -f2 | tr -d ' ')
    local min_consumer_time=$(grep -i "Min.*Consumer.*Time" "$source_csv" | cut -d',' -f2 | tr -d ' ')
    local max_consumer_time=$(grep -i "Max.*Consumer.*Time" "$source_csv" | cut -d',' -f2 | tr -d ' ')
    local total_throughput=$(grep -i "Total.*Throughput" "$source_csv" | cut -d',' -f2 | tr -d ' ')
    
    # Calculate latency
    local avg_latency_ms=$(echo "$avg_consumer_time $ops_per_consumer" | awk '{if($2>0) printf "%.6f", ($1/$2)*1000; else print "0.0"}')
    
    # Get memory usage
    local memory_mb=$(grep -i "Memory" "$source_csv" | cut -d',' -f2 | tr -d ' ')
    if [ -z "$memory_mb" ] || [ "$memory_mb" = "N/A" ]; then
        memory_mb="0.0"
    fi
    
    # Create enhanced CSV matching benchmark.c format
    {
        echo "Test_Name,Queue_Implementation,MPI_Size,Num_Consumers,Ops_Per_Consumer,Total_Operations,Producer_Time_Sec,Producer_Throughput_Ops_Per_Sec,Avg_Consumer_Time_Sec,Min_Consumer_Time_Sec,Max_Consumer_Time_Sec,Total_Consumer_Throughput_Ops_Per_Sec,Avg_Latency_Ms,Memory_Usage_MB"
        echo "\"Micro Benchmark\",\"${queue_name}\",${num_procs},${num_consumers},${ops_per_consumer},$((num_consumers * ops_per_consumer)),${producer_time:-0.0},${producer_throughput:-0.0},${avg_consumer_time:-0.0},${min_consumer_time:-0.0},${max_consumer_time:-0.0},${total_throughput:-0.0},${avg_latency_ms},${memory_mb}"
    } > "$output_csv"
    
    log_success "Generated enhanced summary: $output_csv"
}

# Main execution
log_info "Building microbenchmark executable..."
EXECUTABLE=$(build_microbenchmark_executable "$SPMC_IMPL_PATH")

if [[ $? -ne 0 ]] || [[ -z "$EXECUTABLE" ]]; then
    log_error "Failed to build microbenchmark executable"
    exit 1
fi

echo ""
log_info "Starting microbenchmark execution..."
echo ""

# Run the microbenchmark
if run_microbenchmark "$EXECUTABLE" "$NUM_PROCESSES" "$OPS_PER_CONSUMER" "$SPMC_NAME"; then
    echo ""
    log_success "Microbenchmark completed successfully!"
    echo ""
    
    # Display results summary
    csv_file="${SESSION_DIR}/microbench_${SPMC_NAME}_${NUM_CONSUMERS}consumers_${OPS_PER_CONSUMER}ops.csv"
    
    if [ -f "${csv_file}" ]; then
        echo -e "${GREEN}========================================${NC}"
        echo -e "${GREEN}Results Summary${NC}"
        echo -e "${GREEN}========================================${NC}"
        echo ""
        
        # Extract key metrics from CSV
        throughput=$(grep "Total.*Throughput" "${csv_file}" | cut -d',' -f2)
        avg_time=$(grep "Average Consumer Time" "${csv_file}" | cut -d',' -f2)
        producer_time=$(grep "Producer.*Time" "${csv_file}" | cut -d',' -f2)
        
        echo "SPMC Implementation:    ${SPMC_NAME}"
        echo "Number of Consumers:    ${NUM_CONSUMERS}"
        echo "Operations per Consumer: ${OPS_PER_CONSUMER}"
        echo "Timeout Setting:        ${TIMEOUT} seconds"
        echo ""
        
        if [ ! -z "${throughput}" ] && [ "${throughput}" != "N/A" ]; then
            printf "Total Throughput:       %.2f ops/sec\n" ${throughput}
        fi
        if [ ! -z "${avg_time}" ] && [ "${avg_time}" != "N/A" ]; then
            printf "Avg Consumer Time:      %.6f sec\n" ${avg_time}
            # Calculate latency
            latency_ms=$(echo "$avg_time $OPS_PER_CONSUMER" | awk '{if($2>0) printf "%.6f", ($1/$2)*1000}')
            printf "Avg Latency:            %.6f ms\n" ${latency_ms}
        fi
        if [ ! -z "${producer_time}" ] && [ "${producer_time}" != "N/A" ]; then
            printf "Producer Phase Time:    %.6f sec\n" ${producer_time}
        fi
        
        echo ""
        echo -e "${BLUE}Detailed CSV:${NC} ${csv_file}"
        
        # Show enhanced summary if it exists
        enhanced_csv="${SESSION_DIR}/enhanced_summary.csv"
        if [ -f "${enhanced_csv}" ]; then
            echo -e "${BLUE}Enhanced Summary:${NC} ${enhanced_csv}"
        fi
    fi
else
    log_error "Microbenchmark failed"
    exit 1
fi

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Benchmark Complete!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo -e "${BLUE}Results saved to:${NC} ${SESSION_DIR}"
echo -e "${BLUE}Detailed log:${NC} ${LOG_FILE}"
echo ""
