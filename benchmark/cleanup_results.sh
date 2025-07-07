#!/bin/bash

# Cleanup and organize benchmark results
# Moves all CSV files to results directory

set -e

BENCHMARK_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_DIR="${BENCHMARK_DIR}/results"

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

log_info "Cleaning up and organizing benchmark results..."

# Find all CSV files in benchmark directory (not in subdirectories)
csv_count=0
shopt -s nullglob  # Handle case when no files match pattern

for csv_file in "${BENCHMARK_DIR}"/*.csv; do
    if [[ -f "$csv_file" ]]; then
        filename=$(basename "$csv_file")
        mv "$csv_file" "$RESULTS_DIR/"
        log_info "Moved: $filename → results/"
        csv_count=$((csv_count + 1))
    fi
done

# Check examples directory too
for csv_file in "${BENCHMARK_DIR}/examples"/*.csv; do
    if [[ -f "$csv_file" ]]; then
        filename=$(basename "$csv_file")
        # Check if file already exists in results
        if [[ -f "$RESULTS_DIR/$filename" ]]; then
            log_warning "File already exists: $filename (skipping)"
        else
            mv "$csv_file" "$RESULTS_DIR/"
            log_info "Moved from examples: $filename → results/"
            csv_count=$((csv_count + 1))
        fi
    fi
done

if [[ $csv_count -eq 0 ]]; then
    log_warning "No CSV files found to move"
else
    log_success "Moved $csv_count CSV files to results directory"
fi

log_info "Current files in results directory:"
ls -la "$RESULTS_DIR"

echo ""
log_success "Cleanup completed!"
