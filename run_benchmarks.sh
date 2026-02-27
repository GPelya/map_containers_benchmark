#!/usr/bin/env bash
# run_benchmarks.sh — runs hello_benchmark with progress indicator
# Usage: ./run_benchmarks.sh [extra benchmark flags]
set -euo pipefail

BIN="./build/hello_benchmark"
OUT="results.json"

# Count total benchmarks (--benchmark_list_tests prints one name per line)
TOTAL=$("$BIN" --benchmark_list_tests=true 2>/dev/null | wc -l)
echo "Running ${TOTAL} benchmarks → ${OUT}"
echo ""

COUNT=0
# Run benchmark, intercept lines that start with "BM_", print [N/Total] prefix
"$BIN" --benchmark_out="${OUT}" --benchmark_out_format=json "$@" 2>&1 \
| while IFS= read -r line; do
    # Benchmark result lines start with "BM_"
    if [[ "$line" =~ ^BM_ ]]; then
        COUNT=$((COUNT + 1))
        PCT=$(( COUNT * 100 / TOTAL ))
        printf "[%3d%%  %${#TOTAL}d/%-${#TOTAL}d]  %s\n" \
               "$PCT" "$COUNT" "$TOTAL" "$line"
    else
        echo "$line"
    fi
done

echo ""
echo "Done. Results saved to ${OUT}"

echo "Generating plot..."
python3 plot_results.py "${OUT}"
