#!/bin/bash

# Cleanup and organize benchmark results
# Moves all CSV files to results directory

set -e

BENCHMARK_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_DIR="${BENCHMARK_DIR}/results_micro"

# Colors for output
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

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# Create results directory
mkdir -p "$RESULTS_DIR"

echo ""

log_info "Deleting all folders inside results and logs directories..."

# Function to delete all subdirectories in a given directory
delete_subdirs() {
    local target_dir="$1"
    if [[ -d "$target_dir" ]]; then
        local found=0
        for subdir in "$target_dir"/*/; do
            if [[ -d "$subdir" ]]; then
                rm -rf "$subdir"
                log_info "Deleted folder: $(basename "$subdir") in $(basename "$target_dir")/"
                found=1
            fi
        done
        if [[ $found -eq 0 ]]; then
            log_warning "No folders found in $(basename "$target_dir")/ to delete."
        else
            log_success "Deleted all folders in $(basename "$target_dir")/"
        fi
    else
        log_warning "Directory $target_dir does not exist."
    fi
}


# Delete all subdirectories in results
delete_subdirs "$RESULTS_DIR"

# Delete all files in logs directory
delete_files_in_dir() {
    local target_dir="$1"
    if [[ -d "$target_dir" ]]; then
        local found=0
        for file in "$target_dir"/*; do
            if [[ -f "$file" ]]; then
                rm -f "$file"
                log_info "Deleted file: $(basename "$file") in $(basename "$target_dir")/"
                found=1
            fi
        done
        if [[ $found -eq 0 ]]; then
            log_warning "No files found in $(basename "$target_dir")/ to delete."
        else
            log_success "Deleted all files in $(basename "$target_dir")/"
        fi
    else
        log_warning "Directory $target_dir does not exist."
    fi
}

delete_files_in_dir "${BENCHMARK_DIR}/logs"

log_info "Current contents of results directory:"
ls -la "$RESULTS_DIR"

echo ""
log_success "Cleanup completed!"
