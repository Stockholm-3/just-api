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

# Step 3: Collect library and source object files
LIB_OBJS=$(find "$BUILD_DIR/lib" -name '*.o' 2>/dev/null)
SRC_OBJS=$(find "$BUILD_DIR/src" -name '*.o' ! -path '*/bin/*' 2>/dev/null)

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
# thread_pool pipe tests
# -----------------------------------------------------------------------
echo ""
echo "Compiling thread_pool pipe tests..."
gcc -O1 -g -Wall -Werror \
    $INCLUDES \
    tests/test_thread_pool_pipe.c \
    "$BUILD_DIR/lib/just-lib/thread_pool/thread_pool.o" \
    -o "$BUILD_DIR/test_thread_pool_pipe" \
    -lstdc++ -pthread

echo ""
"$BUILD_DIR/test_thread_pool_pipe"
TPP_RESULT=$?

# -----------------------------------------------------------------------
# compute_config tests
# -----------------------------------------------------------------------
echo ""
echo "Compiling compute_config tests..."
gcc -O1 -g -Wall -Werror \
    $INCLUDES \
    tests/test_compute_config.c \
    -o "$BUILD_DIR/test_compute_config" \
    -lstdc++ -pthread

echo ""
"$BUILD_DIR/test_compute_config"
CC_RESULT=$?

# -----------------------------------------------------------------------
# process_health_check tests
# -----------------------------------------------------------------------
echo ""
echo "Compiling process_health_check tests..."
gcc -O1 -g -Wall -Werror \
    $INCLUDES \
    tests/test_process_health_check.c \
    "$BUILD_DIR/src/watchdog/process.o" \
    "$BUILD_DIR/lib/just-lib/logger/logger/logger.o" \
    -o "$BUILD_DIR/test_process_health_check" \
    -lstdc++ -pthread

echo ""
"$BUILD_DIR/test_process_health_check"
PHC_RESULT=$?

# -----------------------------------------------------------------------
# watchdog_config tests
# -----------------------------------------------------------------------
echo ""
echo "Compiling watchdog_config tests..."
gcc -O1 -g -Wall -Werror \
    $INCLUDES \
    tests/test_watchdog_config.c \
    $LIB_OBJS \
    "$BUILD_DIR/src/config/config_parser.o" \
    -o "$BUILD_DIR/test_watchdog_config" \
    -lmbedtls -lmbedx509 -lmbedcrypto -lstdc++ -pthread

echo ""
"$BUILD_DIR/test_watchdog_config"
WCF_RESULT=$?

# -----------------------------------------------------------------------
# instance_dispose tests
# -----------------------------------------------------------------------
echo ""
echo "Compiling instance_dispose tests..."
gcc -O1 -g -Wall -Werror \
    $INCLUDES \
    tests/test_instance_dispose.c \
    $LIB_OBJS \
    $SRC_OBJS \
    -o "$BUILD_DIR/test_instance_dispose" \
    -lmbedtls -lmbedx509 -lmbedcrypto -lstdc++ -pthread

echo ""
"$BUILD_DIR/test_instance_dispose"
IDD_RESULT=$?

# -----------------------------------------------------------------------
# Exit with failure if any suite failed
# -----------------------------------------------------------------------
if [ "$FC_RESULT" -ne 0 ] || [ "$TP_RESULT" -ne 0 ] || [ "$TPE_RESULT" -ne 0 ] || \
   [ "$TPP_RESULT" -ne 0 ] || [ "$CC_RESULT" -ne 0 ] || \
   [ "$PHC_RESULT" -ne 0 ] || [ "$WCF_RESULT" -ne 0 ] || [ "$IDD_RESULT" -ne 0 ]; then
    exit 1
fi
exit 0
