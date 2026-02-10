#!/bin/bash
#
# run_tests.sh - Build and run file_cache unit tests
#
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build/debug"
TEST_BIN="$BUILD_DIR/test_file_cache"

cd "$PROJECT_DIR"

# Step 1: Build object files via make (ignore linker error)
echo "Building dependencies..."
make 2>/dev/null || true

# Verify file_cache.o exists
if [ ! -f "$BUILD_DIR/src/cache_utils/file_cache.o" ]; then
    echo "ERROR: file_cache.o not found. Run 'make' first."
    exit 1
fi

# Step 2: Collect include paths (same as Makefile)
SRC_INCLUDES=$(find src -type d | sed 's/^/-I/')
LIB_INCLUDES=$(find -L lib -type d | sed 's/^/-I/')
INCLUDES="$SRC_INCLUDES $LIB_INCLUDES -Iincludes"

# Step 3: Collect library object files
LIB_OBJS=$(find "$BUILD_DIR/lib" -name '*.o' 2>/dev/null)

# Step 4: Compile test binary
echo "Compiling tests..."
mkdir -p "$BUILD_DIR"
gcc -O1 -g -Wall -Werror \
    $INCLUDES \
    tests/test_file_cache.c \
    "$BUILD_DIR/src/cache_utils/file_cache.o" \
    "$BUILD_DIR/src/api/hash_md5.o" \
    $LIB_OBJS \
    -o "$TEST_BIN" \
    -lmbedtls -lmbedx509 -lmbedcrypto

# Step 5: Run tests
echo ""
"$TEST_BIN"
exit $?
