#!/bin/bash
#
# run_tests.sh - Build and run unit tests
#

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
if gcc -O1 -g -Wall -Werror \
    $INCLUDES \
    tests/test_file_cache.c \
    "$BUILD_DIR/src/cache_utils/file_cache.o" \
    "$BUILD_DIR/src/api/hash_md5.o" \
    "$BUILD_DIR/src/logger/logger.o" \
    $LIB_OBJS \
    -o "$BUILD_DIR/test_file_cache" \
    -lmbedtls -lmbedx509 -lmbedcrypto -lstdc++ -pthread; then
    echo ""
    "$BUILD_DIR/test_file_cache"
    FC_RESULT=$?
else
    echo "COMPILE ERROR: test_file_cache — skipping run"
    FC_RESULT=1
fi

# -----------------------------------------------------------------------
# thread_pool tests
# -----------------------------------------------------------------------
echo ""
echo "Compiling thread_pool tests..."
if gcc -O1 -g -Wall -Werror \
    $INCLUDES \
    tests/test_thread_pool.c \
    "$BUILD_DIR/lib/just-lib/thread_pool/thread_pool.o" \
    -o "$BUILD_DIR/test_thread_pool" \
    -lstdc++ -pthread; then
    echo ""
    "$BUILD_DIR/test_thread_pool"
    TP_RESULT=$?
else
    echo "COMPILE ERROR: test_thread_pool — skipping run"
    TP_RESULT=1
fi

# -----------------------------------------------------------------------
# thread_pool exception tests (C++)
# -----------------------------------------------------------------------
echo ""
echo "Compiling thread_pool exception tests..."
if g++ -O1 -g -Wall -Werror -std=c++11 \
    $INCLUDES \
    tests/test_thread_pool_exception.cpp \
    "$BUILD_DIR/lib/just-lib/thread_pool/thread_pool.o" \
    -o "$BUILD_DIR/test_thread_pool_exception" \
    -lstdc++ -pthread; then
    echo ""
    "$BUILD_DIR/test_thread_pool_exception"
    TPE_RESULT=$?
else
    echo "COMPILE ERROR: test_thread_pool_exception — skipping run"
    TPE_RESULT=1
fi

# -----------------------------------------------------------------------
# compute_config tests
# -----------------------------------------------------------------------
echo ""
echo "Compiling compute_config tests..."
if gcc -O1 -g -Wall -Werror \
    $INCLUDES \
    tests/test_compute_config.c \
    "$BUILD_DIR/src/energy_plan/compute.o" \
    "$BUILD_DIR/src/logger/logger.o" \
    $LIB_OBJS \
    -o "$BUILD_DIR/test_compute_config" \
    -ljansson -lmbedtls -lmbedx509 -lmbedcrypto -lstdc++ -pthread; then
    echo ""
    "$BUILD_DIR/test_compute_config"
    CC_RESULT=$?
else
    echo "COMPILE ERROR: test_compute_config — skipping run"
    CC_RESULT=1
fi

# -----------------------------------------------------------------------
# fetch_scheduler tests
# -----------------------------------------------------------------------
echo ""
echo "Compiling fetch_scheduler tests..."
if gcc -O1 -g -Wall -Werror \
    $INCLUDES \
    tests/test_fetch_scheduler.c \
    $SRC_OBJS \
    $LIB_OBJS \
    -o "$BUILD_DIR/test_fetch_scheduler" \
    -ljansson -lmbedtls -lmbedx509 -lmbedcrypto -lstdc++ -pthread; then
    echo ""
    "$BUILD_DIR/test_fetch_scheduler"
    FS_RESULT=$?
else
    echo "COMPILE ERROR: test_fetch_scheduler — skipping run"
    FS_RESULT=1
fi

# -----------------------------------------------------------------------
# weather_server_instance tests
# -----------------------------------------------------------------------
echo ""
echo "Compiling weather_server_instance tests..."
if gcc -O1 -g -Wall -Werror \
    $INCLUDES \
    tests/test_weather_server_instance.c \
    $SRC_OBJS \
    $LIB_OBJS \
    -o "$BUILD_DIR/test_weather_server_instance" \
    -ljansson -lmbedtls -lmbedx509 -lmbedcrypto -lstdc++ -pthread; then
    echo ""
    "$BUILD_DIR/test_weather_server_instance"
    WSI_RESULT=$?
else
    echo "COMPILE ERROR: test_weather_server_instance — skipping run"
    WSI_RESULT=1
fi

# -----------------------------------------------------------------------
# weather_server tests
# -----------------------------------------------------------------------
echo ""
echo "Compiling weather_server tests..."
if gcc -O1 -g -Wall -Werror \
    $INCLUDES \
    tests/test_weather_server.c \
    $SRC_OBJS \
    $LIB_OBJS \
    -o "$BUILD_DIR/test_weather_server" \
    -ljansson -lmbedtls -lmbedx509 -lmbedcrypto -lstdc++ -pthread; then
    echo ""
    "$BUILD_DIR/test_weather_server"
    WS_RESULT=$?
else
    echo "COMPILE ERROR: test_weather_server — skipping run"
    WS_RESULT=1
fi

# -----------------------------------------------------------------------
# Exit with failure if any suite failed
# -----------------------------------------------------------------------
if [ "$FC_RESULT" -ne 0 ] || [ "$TP_RESULT" -ne 0 ] || [ "$TPE_RESULT" -ne 0 ] || \
   [ "$CC_RESULT" -ne 0 ] || [ "$FS_RESULT" -ne 0 ] || \
   [ "$WSI_RESULT" -ne 0 ] || [ "$WS_RESULT" -ne 0 ]; then
    exit 1
fi
exit 0
