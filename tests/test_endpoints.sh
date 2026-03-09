#!/bin/bash
#
# test_endpoints.sh - Test all HTTP endpoints
#
# Usage: ./test_endpoints.sh [--local] [--url=http://host:port]
#   --local          skip build and server start; test an already-running server
#   --url=<url>      override base URL (default: http://localhost:10680)
#
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BASE_URL="http://localhost:10680"
SERVER_PID=""
PASSED=0
FAILED=0
PASSED_LIST=""
FAILED_LIST=""
LOCAL=0

for arg in "$@"; do
    case "$arg" in
        --local)   LOCAL=1 ;;
        --url=*)   BASE_URL="${arg#--url=}" ;;
    esac
done

cleanup() {
    if [ $LOCAL -eq 0 ] && [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null
    fi
}
trap cleanup EXIT

cd "$PROJECT_DIR"

if [ $LOCAL -eq 0 ]; then
    echo "Building..."
    make all -j"$(nproc)" > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        echo "FAIL: Build failed"
        exit 1
    fi

    echo "Starting server..."
    ./build/debug/server &
    SERVER_PID=$!

    # Wait for server to be ready
    for i in $(seq 1 30); do
        if curl -s -o /dev/null "$BASE_URL/health" 2>/dev/null; then
            break
        fi
        if ! kill -0 "$SERVER_PID" 2>/dev/null; then
            echo "FAIL: Server crashed on startup"
            exit 1
        fi
        sleep 0.2
    done

    if ! curl -s -o /dev/null "$BASE_URL/health" 2>/dev/null; then
        echo "FAIL: Server did not start within 6 seconds"
        exit 1
    fi
else
    echo "Local mode — using running server at $BASE_URL"
    if ! curl -s -o /dev/null "$BASE_URL/health" 2>/dev/null; then
        echo "FAIL: No server reachable at $BASE_URL"
        exit 1
    fi
fi

echo ""
echo "Running endpoint tests..."
echo "========================="

test_endpoint_2xx() {
    local method="$1"
    local path="$2"
    local expect_body="$3"
    local label="$method $path"

    local response
    local http_code
    local body

    response=$(curl -s -w "\n%{http_code}" "$BASE_URL$path" 2>/dev/null)
    http_code=$(echo "$response" | tail -1)
    body=$(echo "$response" | sed '$d')

    if [ "$http_code" != "200" ] && [ "$http_code" != "202" ] && [ "$http_code" != "204" ]; then
        echo "FAIL  $label  (expected 2xx, got $http_code)"
        FAILED=$((FAILED + 1))
        FAILED_LIST="${FAILED_LIST}    $label  (expected 2xx, got $http_code)\n"
        return
    fi

    if [ -n "$expect_body" ]; then
        if ! echo "$body" | grep -q "$expect_body"; then
            echo "FAIL  $label  (missing: $expect_body)"
            FAILED=$((FAILED + 1))
            FAILED_LIST="${FAILED_LIST}    $label  (missing: $expect_body)\n"
            return
        fi
    fi

    echo "PASS  $label  ($http_code)"
    PASSED=$((PASSED + 1))
    PASSED_LIST="${PASSED_LIST}    $label\n"
}

test_endpoint() {
    local method="$1"
    local path="$2"
    local expect_code="$3"
    local expect_body="$4"
    local label="$method $path"

    local response
    local http_code
    local body

    if [ "$method" = "POST" ]; then
        response=$(curl -s -w "\n%{http_code}" -X POST -d "test" "$BASE_URL$path" 2>/dev/null)
    else
        response=$(curl -s -w "\n%{http_code}" "$BASE_URL$path" 2>/dev/null)
    fi

    http_code=$(echo "$response" | tail -1)
    body=$(echo "$response" | sed '$d')

    # Check HTTP status
    if [ "$http_code" != "$expect_code" ]; then
        echo "FAIL  $label  (expected $expect_code, got $http_code)"
        FAILED=$((FAILED + 1))
        FAILED_LIST="${FAILED_LIST}    $label  (expected $expect_code, got $http_code)\n"
        return
    fi

    # Check body content if specified
    if [ -n "$expect_body" ]; then
        if ! echo "$body" | grep -q "$expect_body"; then
            echo "FAIL  $label  (missing: $expect_body)"
            FAILED=$((FAILED + 1))
            FAILED_LIST="${FAILED_LIST}    $label  (missing: $expect_body)\n"
            return
        fi
    fi

    echo "PASS  $label"
    PASSED=$((PASSED + 1))
    PASSED_LIST="${PASSED_LIST}    $label\n"
}

test_endpoint "GET"  "/"                                    200 ""
test_endpoint "GET"  "/health"                              200 "status"
test_endpoint "GET"  "/echo"                                200 ""
test_endpoint "POST" "/echo"                                200 ""
test_endpoint "GET"  "/v1/weather?city=Kyiv"                200 '"success"'
test_endpoint "GET"  "/v1/current?lat=59.33&lon=18.07"      200 '"success"'
test_endpoint "GET"  "/v1/hourly?city=Stockholm&hours=3"    200 '"success"'
test_endpoint "GET"  "/v1/forecast?lat=59.33&lon=18.07"     200 ""
test_endpoint "GET"  "/v1/cities?query=Stock"               200 ""
test_endpoint "GET"  "/v1/elpris?price=SE3"                 200 ""
test_endpoint "GET"  "/nonexistent"                         404 ""

# Minutely and aliased forecast routes
test_endpoint "GET" "/v1/minutely?lat=59.33&lon=18.07&hours=3"  200 '"success"'
test_endpoint "GET" "/v1/forecast/minutely?lat=59.33&lon=18.07" 200 ""
test_endpoint "GET" "/v1/forecast/hourly?lat=59.33&lon=18.07"   200 ""

# /v1/get_plan — validation (deterministic)
test_endpoint "GET" "/v1/get_plan"                                 400 ""
test_endpoint "GET" "/v1/get_plan?city=Stockholm"                  400 ""
test_endpoint "GET" "/v1/get_plan?price=SE3"                       400 ""
test_endpoint "GET" "/v1/get_plan?city=Stockholm&price=INVALID"    400 ""
test_endpoint "GET" "/v1/get_plan?city=Nonexistent9999&price=SE3"  400 ""

# /v1/get_plan — happy path (200 if data ready, 202 if newly registered)
test_endpoint_2xx "GET" "/v1/get_plan?city=Stockholm&price=SE3" ""

echo "========================="
echo ""
echo "Results: $PASSED passed, $FAILED failed"
echo ""
if [ "$PASSED" -gt 0 ]; then
    echo "Working endpoints:"
    echo -e "$PASSED_LIST"
fi
if [ "$FAILED" -gt 0 ]; then
    echo "Failed endpoints:"
    echo -e "$FAILED_LIST"
    exit 1
fi
exit 0
