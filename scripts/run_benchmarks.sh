#!/bin/bash
#
# VLife Benchmark Runner Script
# Runs all benchmarks and collects results for publication
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_ROOT}/build"
RESULTS_DIR="${PROJECT_ROOT}/benchmark_results"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

echo_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

echo_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Parse arguments
RUN_STANDARD=true
RUN_EXTENDED=true
NUM_RUNS=1
BUILD_FIRST=true

while [[ $# -gt 0 ]]; do
    case $1 in
        --standard-only)
            RUN_EXTENDED=false
            shift
            ;;
        --extended-only)
            RUN_STANDARD=false
            shift
            ;;
        --runs)
            NUM_RUNS="$2"
            shift 2
            ;;
        --no-build)
            BUILD_FIRST=false
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  --standard-only   Run only standard benchmarks"
            echo "  --extended-only   Run only extended benchmarks"
            echo "  --runs N          Run benchmarks N times (default: 1)"
            echo "  --no-build        Skip building (use existing build)"
            echo "  --help, -h        Show this help"
            exit 0
            ;;
        *)
            echo_error "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Create results directory
mkdir -p "$RESULTS_DIR"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
RUN_RESULTS_DIR="${RESULTS_DIR}/${TIMESTAMP}"
mkdir -p "$RUN_RESULTS_DIR"

echo_info "VLife Benchmark Suite"
echo_info "====================="
echo_info "Results will be saved to: ${RUN_RESULTS_DIR}"

# Build if requested
if [ "$BUILD_FIRST" = true ]; then
    echo_info "Building VLife in Release mode..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake -DCMAKE_BUILD_TYPE=Release ..
    make -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
    cd "$PROJECT_ROOT"
fi

# Check executables exist
if [ ! -f "${BUILD_DIR}/VLifeBenchmark" ]; then
    echo_error "VLifeBenchmark not found. Please build first."
    exit 1
fi

if [ ! -f "${BUILD_DIR}/VLifeExtendedBenchmark" ]; then
    echo_error "VLifeExtendedBenchmark not found. Please build first."
    exit 1
fi

# Collect system info
echo_info "Collecting system information..."
{
    echo "=== System Information ==="
    echo "Date: $(date)"
    echo "Hostname: $(hostname)"
    echo ""

    if [[ "$(uname)" == "Darwin" ]]; then
        echo "=== macOS Info ==="
        sw_vers
        echo ""
        echo "=== Hardware ==="
        sysctl -n machdep.cpu.brand_string 2>/dev/null || echo "N/A"
        echo "Cores: $(sysctl -n hw.ncpu)"
        echo "Memory: $(sysctl -n hw.memsize | awk '{print $1/1024/1024/1024 " GB"}')"
    else
        echo "=== Linux Info ==="
        uname -a
        echo ""
        echo "=== CPU ==="
        cat /proc/cpuinfo | grep "model name" | head -1
        echo "Cores: $(nproc)"
        echo "Memory: $(free -h | grep Mem | awk '{print $2}')"
    fi
    echo ""
} > "${RUN_RESULTS_DIR}/system_info.txt"

cat "${RUN_RESULTS_DIR}/system_info.txt"

# Run benchmarks
for run in $(seq 1 $NUM_RUNS); do
    if [ $NUM_RUNS -gt 1 ]; then
        echo_info "=== Run $run of $NUM_RUNS ==="
    fi

    # Standard benchmarks
    if [ "$RUN_STANDARD" = true ]; then
        echo_info "Running standard benchmarks..."
        "${BUILD_DIR}/VLifeBenchmark" 2>&1 | tee "${RUN_RESULTS_DIR}/standard_run${run}.txt"
    fi

    # Extended benchmarks
    if [ "$RUN_EXTENDED" = true ]; then
        echo_info "Running extended benchmarks..."
        "${BUILD_DIR}/VLifeExtendedBenchmark" 2>&1 | tee "${RUN_RESULTS_DIR}/extended_run${run}.txt"
    fi
done

# Create summary
echo_info "Creating summary..."
{
    echo "=== Benchmark Summary ==="
    echo "Timestamp: $TIMESTAMP"
    echo "Number of runs: $NUM_RUNS"
    echo ""

    if [ "$RUN_STANDARD" = true ]; then
        echo "=== Standard Benchmark Results (Run 1) ==="
        cat "${RUN_RESULTS_DIR}/standard_run1.txt"
        echo ""
    fi

    if [ "$RUN_EXTENDED" = true ]; then
        echo "=== Extended Benchmark Results (Run 1) ==="
        cat "${RUN_RESULTS_DIR}/extended_run1.txt"
        echo ""
    fi
} > "${RUN_RESULTS_DIR}/summary.txt"

echo_info "Benchmarks complete!"
echo_info "Results saved to: ${RUN_RESULTS_DIR}"
echo_info ""
echo_info "Files created:"
ls -la "${RUN_RESULTS_DIR}"
