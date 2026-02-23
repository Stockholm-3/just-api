#!/bin/bash
#
# run_tests.sh - Build and run unit tests
#
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build/debug"

cd "$PROJECT_DIR"

# Step 1: Build object files via make
echo "Building dependencies..."
make 2>/dev/null || true

# Verify required objects exist
if [ ! -f "$BUILD_DIR/src/cache_utils/file_cache.o" ]; then
    echo "ERROR: file_cache.o not found. Run 'make' first."
    exit 1
fi
if [ ! -f "$BUILD_DIR/lib/just-lib/thread_pool/thread_pool.o" ]; then
    echo "ERROR: thread_pool.o not found. Run 'make' first."
    exit 1
fi

# Step 2: Collect include paths (same as Makefile)
SRC_INCLUDES=$(find src -type d | sed 's/^/-I/')
LIB_INCLUDES=$(find -L lib -type d | sed 's/^/-I/')
INCLUDES="$SRC_INCLUDES $LIB_INCLUDES -Iincludes"

# Step 3: Collect library object files
LIB_OBJS=$(find "$BUILD_DIR/lib" -name '*.o' 2>/dev/null)

mkdir -p "$BUILD_DIR"

# -----------------------------------------------------------------------
# file_cache tests
# -----------------------------------------------------------------------
echo "Compiling file_cache tests..."
gcc -O1 -g -Wall -Werror \
    $INCLUDES \
    tests/test_file_cache.c \
    "$BUILD_DIR/src/cache_utils/file_cache.o" \
    "$BUILD_DIR/src/api/hash_md5.o" \
    "$BUILD_DIR/src/logger/logger.o" \
    $LIB_OBJS \
    -o "$BUILD_DIR/test_file_cache" \
    -lmbedtls -lmbedx509 -lmbedcrypto -lstdc++ -pthread

echo ""
"$BUILD_DIR/test_file_cache"
FC_RESULT=$?

# -----------------------------------------------------------------------
# thread_pool tests
# -----------------------------------------------------------------------
echo ""
echo "Compiling thread_pool tests..."
gcc -O1 -g -Wall -Werror \
    $INCLUDES \
    tests/test_thread_pool.c \
    "$BUILD_DIR/lib/just-lib/thread_pool/thread_pool.o" \
    -o "$BUILD_DIR/test_thread_pool" \
    -lstdc++ -pthread

echo ""
"$BUILD_DIR/test_thread_pool"
TP_RESULT=$?

# -----------------------------------------------------------------------
# thread_pool exception tests (C++)
# -----------------------------------------------------------------------
echo ""
echo "Compiling thread_pool exception tests..."
g++ -O1 -g -Wall -Werror -std=c++11 \
    $INCLUDES \
    tests/test_thread_pool_exception.cpp \
    "$BUILD_DIR/lib/just-lib/thread_pool/thread_pool.o" \
    -o "$BUILD_DIR/test_thread_pool_exception" \
    -lstdc++ -pthread

echo ""
"$BUILD_DIR/test_thread_pool_exception"
TPE_RESULT=$?

# -----------------------------------------------------------------------
# Exit with failure if any suite failed
# -----------------------------------------------------------------------
if [ "$FC_RESULT" -ne 0 ] || [ "$TP_RESULT" -ne 0 ] || [ "$TPE_RESULT" -ne 0 ]; then
    exit 1
fi
exit 0
