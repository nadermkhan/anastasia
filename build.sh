#!/usr/bin/env bash
# ==============================================================================
# Anastasia Engine Bare-Metal Build Script
# High-Throughput Freestanding Anastasia JIT/AOT Compiler System
# ==============================================================================

set -e

BUILD_DIR="build"
BUILD_TYPE="Release"
RUN_TESTS=0
RUN_BENCH=0
CLEAN_BUILD=0

usage() {
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  -c, --clean        Clean build directory before compiling"
    echo "  -d, --debug        Configure build with Debug mode"
    echo "  -t, --test         Run full test suite after building"
    echo "  -b, --bench        Run benchmark suite after building"
    echo "  -h, --help         Show this help message"
    echo ""
    exit 0
}

# Parse command line flags
while [[ $# -gt 0 ]]; do
    case "$1" in
        -c|--clean)
            CLEAN_BUILD=1
            shift
            ;;
        -d|--debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        -t|--test)
            RUN_TESTS=1
            shift
            ;;
        -b|--bench)
            RUN_BENCH=1
            shift
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "Unknown option: $1"
            usage
            ;;
    esac
done

# Clean build directory if requested
if [ "$CLEAN_BUILD" -eq 1 ] && [ -d "$BUILD_DIR" ]; then
    echo "[*] Cleaning build directory ($BUILD_DIR)..."
    rm -rf "$BUILD_DIR"
fi

# Detect CPU cores for parallel compilation
if command -v nproc >/dev/null 2>&1; then
    JOBS=$(nproc)
elif command -v sysctl >/dev/null 2>&1; then
    JOBS=$(sysctl -n hw.ncpu)
else
    JOBS=4
fi

echo "[*] Configuring Anastasia Engine ($BUILD_TYPE mode)..."
cmake -B "$BUILD_DIR" -S . -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

echo "[*] Compiling targets using $JOBS parallel workers..."
cmake --build "$BUILD_DIR" -j "$JOBS"

echo "[+] Anastasia Engine built successfully in $BUILD_DIR/"
echo "    Binaries generated:"
echo "    - $BUILD_DIR/anastasia_engine"
echo "    - $BUILD_DIR/anastasia_benchmark"

if [ "$RUN_TESTS" -eq 1 ]; then
    echo ""
    echo "[*] Running Anastasia QA Test Suite..."
    "./$BUILD_DIR/anastasia_engine"
fi

if [ "$RUN_BENCH" -eq 1 ]; then
    echo ""
    echo "[*] Running Anastasia Benchmark Suite..."
    "./$BUILD_DIR/anastasia_benchmark"
fi
