#!/bin/bash

# SPMC Results Analysis Script
# Tool for analyzing and comparing benchmark results across different SPMC types

set -e

# Configuration
BENCHMARK_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_DIR="${BENCHMARK_DIR}/results"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
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

print_header() {
    echo -e "${CYAN}$1${NC}"
}

print_usage() {
    echo ""
    echo "SPMC Results Analysis Tool"
    echo "=========================="
    echo ""
    echo "Usage: $0 [COMMAND] [OPTIONS]"
    echo ""
    echo "Commands:"
    echo "  list         - List all available results by SPMC type"
    echo "  summary      - Show summary of all results"
    echo "  compare      - Compare results between SPMC types"
    echo "  details TYPE - Show detailed results for specific SPMC type"
    echo "  latest       - Show latest results for each SPMC type"
    echo "  clean        - Clean old result files (keep latest 10)"
    echo ""
    echo "Options:"
    echo "  -v, --verbose    Enable verbose output"
    echo "  -h, --help       Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0 list"
    echo "  $0 summary"
    echo "  $0 details spmc_2004"
    echo "  $0 compare"
    echo ""
}

# Function to list available SPMC types and their results
list_results() {
    log_info "Available SPMC Results:"
    print_separator
    
    if [[ ! -d "$RESULTS_DIR" ]]; then
        log_warning "No results directory found at: $RESULTS_DIR"
        return
    fi
    
    local found_any=false
    for spmc_dir in "$RESULTS_DIR"/*/; do
        if [[ -d "$spmc_dir" ]]; then
            local spmc_type=$(basename "$spmc_dir")
            local csv_count=$(find "$spmc_dir" -name "*.csv" | wc -l)
            
            print_header "SPMC Type: $spmc_type"
            echo "  Directory: $spmc_dir"
            echo "  CSV Files: $csv_count"
            
            if [[ $csv_count -gt 0 ]]; then
                echo "  Latest results:"
                find "$spmc_dir" -name "*.csv" -type f -printf "    %f\n" | sort | tail -3
            fi
            echo ""
            found_any=true
        fi
    done
    
    if [[ "$found_any" != "true" ]]; then
        log_warning "No SPMC result directories found"
    fi
}

# Function to show summary of all results
show_summary() {
    log_info "Results Summary:"
    print_separator
    
    local total_files=0
    local total_types=0
    
    for spmc_dir in "$RESULTS_DIR"/*/; do
        if [[ -d "$spmc_dir" ]]; then
            local spmc_type=$(basename "$spmc_dir")
            local csv_count=$(find "$spmc_dir" -name "*.csv" | wc -l)
            local latest_file=$(find "$spmc_dir" -name "*.csv" -type f | sort | tail -1)
            local latest_date=""
            
            if [[ -n "$latest_file" ]]; then
                latest_date=$(stat -c %y "$latest_file" 2>/dev/null | cut -d' ' -f1 || echo "unknown")
            fi
            
            printf "%-15s | %-10s | %s\n" "$spmc_type" "$csv_count files" "Latest: $latest_date"
            total_files=$((total_files + csv_count))
            total_types=$((total_types + 1))
        fi
    done
    
    echo ""
    log_success "Total: $total_types SPMC types, $total_files result files"
}

# Function to show detailed results for a specific SPMC type
show_details() {
    local spmc_type="$1"
    local spmc_dir="$RESULTS_DIR/$spmc_type"
    
    if [[ ! -d "$spmc_dir" ]]; then
        log_error "No results found for SPMC type: $spmc_type"
        return 1
    fi
    
    log_info "Detailed results for: $spmc_type"
    print_separator
    
    local csv_files=($(find "$spmc_dir" -name "*.csv" -type f | sort))
    
    if [[ ${#csv_files[@]} -eq 0 ]]; then
        log_warning "No CSV files found in: $spmc_dir"
        return
    fi
    
    echo "Found ${#csv_files[@]} result files:"
    echo ""
    
    for csv_file in "${csv_files[@]}"; do
        local filename=$(basename "$csv_file")
        local filesize=$(stat -c %s "$csv_file" 2>/dev/null || echo "0")
        local filedate=$(stat -c %y "$csv_file" 2>/dev/null | cut -d' ' -f1-2 || echo "unknown")
        
        printf "%-50s | %8s bytes | %s\n" "$filename" "$filesize" "$filedate"
        
        # Show a preview of the CSV content if verbose mode
        if [[ "$VERBOSE" == "true" ]] && [[ -f "$csv_file" ]]; then
            echo "    Preview:"
            head -3 "$csv_file" | sed 's/^/      /'
            echo ""
        fi
    done
}

# Function to compare results between SPMC types
compare_results() {
    log_info "Comparing results across SPMC types:"
    print_separator
    
    local spmc_types=()
    for spmc_dir in "$RESULTS_DIR"/*/; do
        if [[ -d "$spmc_dir" ]]; then
            spmc_types+=($(basename "$spmc_dir"))
        fi
    done
    
    if [[ ${#spmc_types[@]} -lt 2 ]]; then
        log_warning "Need at least 2 SPMC types to compare. Found: ${#spmc_types[@]}"
        return
    fi
    
    echo "Available SPMC types for comparison:"
    for type in "${spmc_types[@]}"; do
        local file_count=$(find "$RESULTS_DIR/$type" -name "*.csv" | wc -l)
        printf "  %-15s (%d files)\n" "$type" "$file_count"
    done
    
    echo ""
    log_info "Latest benchmark comparison:"
    printf "%-15s | %-20s | %-15s | %s\n" "SPMC Type" "Latest File" "Date" "Size"
    echo "$(printf '%*s' 80 '' | tr ' ' '-')"
    
    for type in "${spmc_types[@]}"; do
        local latest_file=$(find "$RESULTS_DIR/$type" -name "*.csv" -type f | sort | tail -1)
        if [[ -n "$latest_file" ]]; then
            local filename=$(basename "$latest_file")
            local filedate=$(stat -c %y "$latest_file" 2>/dev/null | cut -d' ' -f1 || echo "unknown")
            local filesize=$(stat -c %s "$latest_file" 2>/dev/null || echo "0")
            printf "%-15s | %-20s | %-15s | %s bytes\n" "$type" "$filename" "$filedate" "$filesize"
        else
            printf "%-15s | %-20s | %-15s | %s\n" "$type" "No files" "-" "-"
        fi
    done
}

# Function to show latest results for each SPMC type
show_latest() {
    log_info "Latest results for each SPMC type:"
    print_separator
    
    for spmc_dir in "$RESULTS_DIR"/*/; do
        if [[ -d "$spmc_dir" ]]; then
            local spmc_type=$(basename "$spmc_dir")
            local latest_file=$(find "$spmc_dir" -name "*.csv" -type f | sort | tail -1)
            
            if [[ -n "$latest_file" ]]; then
                print_header "Latest result for $spmc_type:"
                echo "File: $(basename "$latest_file")"
                echo "Path: $latest_file"
                echo "Date: $(stat -c %y "$latest_file" 2>/dev/null | cut -d' ' -f1-2 || echo "unknown")"
                
                if [[ "$VERBOSE" == "true" ]]; then
                    echo "Preview:"
                    head -5 "$latest_file" | sed 's/^/  /'
                fi
                echo ""
            else
                print_header "$spmc_type: No results found"
                echo ""
            fi
        fi
    done
}

# Function to clean old result files
clean_results() {
    log_info "Cleaning old result files (keeping latest 10 per SPMC type)..."
    
    local total_removed=0
    
    for spmc_dir in "$RESULTS_DIR"/*/; do
        if [[ -d "$spmc_dir" ]]; then
            local spmc_type=$(basename "$spmc_dir")
            local csv_files=($(find "$spmc_dir" -name "*.csv" -type f | sort))
            local file_count=${#csv_files[@]}
            
            if [[ $file_count -gt 10 ]]; then
                local to_remove=$((file_count - 10))
                log_info "Removing $to_remove old files from $spmc_type..."
                
                for ((i=0; i<to_remove; i++)); do
                    rm "${csv_files[$i]}"
                    echo "  Removed: $(basename "${csv_files[$i]}")"
                    total_removed=$((total_removed + 1))
                done
            else
                log_info "$spmc_type: $file_count files (no cleanup needed)"
            fi
        fi
    done
    
    log_success "Cleanup completed. Removed $total_removed files total."
}

# Default values
VERBOSE=false
COMMAND=""

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        list|summary|compare|latest|clean)
            COMMAND="$1"
            shift
            ;;
        details)
            COMMAND="details"
            if [[ -n "$2" && "$2" != -* ]]; then
                SPMC_TYPE="$2"
                shift 2
            else
                log_error "details command requires SPMC type argument"
                exit 1
            fi
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -h|--help)
            print_usage
            exit 0
            ;;
        *)
            log_error "Unknown option: $1"
            print_usage
            exit 1
            ;;
    esac
done

# Set default command
if [[ -z "$COMMAND" ]]; then
    COMMAND="summary"
fi

# Main execution
print_separator
log_info "SPMC Results Analysis Tool"
print_separator

case "$COMMAND" in
    list)
        list_results
        ;;
    summary)
        show_summary
        ;;
    details)
        show_details "$SPMC_TYPE"
        ;;
    compare)
        compare_results
        ;;
    latest)
        show_latest
        ;;
    clean)
        clean_results
        ;;
    *)
        log_error "Unknown command: $COMMAND"
        print_usage
        exit 1
        ;;
esac

print_separator
log_success "Analysis completed!"
