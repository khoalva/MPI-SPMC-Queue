#!/bin/bash

# Example script demonstrating the new SPMC type-organized benchmark system

echo "=== SPMC Benchmark Demo with Type Organization ==="
echo ""

# Show what SPMC types are available
echo "1. Detecting available SPMC implementations..."
echo ""

BENCHMARK_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_DIR="${BENCHMARK_DIR}/.."

echo "Available SPMC implementations in workspace:"
for dir in "${WORKSPACE_DIR}"/spmc_*/; do
    if [[ -d "$dir" ]]; then
        echo "  - $(basename "$dir")"
    fi
done
echo ""

# Run a quick benchmark
echo "2. Running a quick benchmark..."
echo "   (The script will auto-detect and let you choose the SPMC type)"
echo ""

# Uncomment the line below to actually run the benchmark
# ./run_benchmarks.sh quick

echo "3. After running benchmarks, results will be organized like this:"
echo ""
echo "benchmark/results/"
echo "├── spmc_2004/"
echo "│   ├── spmc_2004_quick_3procs_20250107_140530.csv"
echo "│   └── spmc_2004_throughput_4procs_20250107_140545.csv"
echo "└── [other_spmc_types]/"
echo ""

echo "4. Use the analysis tool to manage results:"
echo ""
echo "   # List all results by type"
echo "   ./analyze_results.sh list"
echo ""
echo "   # Show summary"
echo "   ./analyze_results.sh summary"
echo ""
echo "   # Compare different SPMC implementations"
echo "   ./analyze_results.sh compare"
echo ""
echo "   # Clean old results (keep latest 10 per type)"
echo "   ./analyze_results.sh clean"
echo ""

echo "=== Benefits of this organization ==="
echo ""
echo "✅ Results are separated by SPMC implementation type"
echo "✅ Easy to compare performance between different implementations"
echo "✅ File names include SPMC type for clarity"
echo "✅ Analysis tools help manage and review results"
echo "✅ Automatic detection of available SPMC types"
echo "✅ No more manual configuration of SPMC types"
echo ""

echo "=== Demo completed! ==="
