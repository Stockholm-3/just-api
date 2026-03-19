#!/bin/bash
#
# test_watchdog_integration.sh - End-to-end watchdog health check + 503 tests
#
# Requires real binaries (run 'make' first).  Starts real processes and makes
# real network connections.  Run manually — NOT part of run_tests.sh because
# it takes ~20s and hits the external open-meteo API (test 3).
#
# Usage:
#   ./tests/test_watchdog_integration.sh
#
# Exit code: 0 if all tests pass, 1 if any fail.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build/debug"
SERVER_BIN="$BUILD_DIR/server"
WATCHDOG_BIN="$BUILD_DIR/watchdog"

# ---------------------------------------------------------------------------
# Test harness
# ---------------------------------------------------------------------------

PASSED=0
FAILED=0

pass() { echo "[PASS] $1"; PASSED=$((PASSED + 1)); }
fail() { echo "[FAIL] $1"; FAILED=$((FAILED + 1)); }

# ---------------------------------------------------------------------------
# Per-test global state (reset by cleanup)
# ---------------------------------------------------------------------------

WATCHDOG_PID=""
INITIAL_SERVER_PID=""
TMPDIR_TEST=""

cleanup() {
    # Send SIGTERM to watchdog so it can kill the server cleanly.
    if [ -n "$WATCHDOG_PID" ]; then
        kill "$WATCHDOG_PID" 2>/dev/null || true
        # Wait up to 3 s for the watchdog to exit on its own.
        local i=0
        while kill -0 "$WATCHDOG_PID" 2>/dev/null && [ "$i" -lt 30 ]; do
            sleep 0.1
            i=$((i + 1))
        done
        kill -9 "$WATCHDOG_PID" 2>/dev/null || true
        wait "$WATCHDOG_PID" 2>/dev/null || true
    fi

    # The initial server is in its own process group (setpgid).  If the
    # watchdog already killed it that's fine; SIGKILL on a dead PID is a no-op.
    # SIGCONT first so SIGKILL is actually delivered to a stopped process.
    if [ -n "$INITIAL_SERVER_PID" ]; then
        kill -CONT "$INITIAL_SERVER_PID" 2>/dev/null || true
        kill -9    "$INITIAL_SERVER_PID" 2>/dev/null || true
    fi

    [ -n "$TMPDIR_TEST" ] && rm -rf "$TMPDIR_TEST"

    WATCHDOG_PID=""
    INITIAL_SERVER_PID=""
    TMPDIR_TEST=""
}
trap cleanup EXIT

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

# write_watchdog_config <dir> <hc_interval_ms> <hc_timeout_ms> <hc_failures> <sigkill_ms>
write_watchdog_config() {
    local dir="$1" hci="$2" hct="$3" hcf="$4" skt="$5"
    cat > "$dir/config.json" <<EOF
{
    "watchdog": {
        "health_check_interval_ms": $hci,
        "health_check_timeout_ms":  $hct,
        "health_check_failures":    $hcf,
        "sigkill_timeout_ms":       $skt,
        "server_ready_wait_ms":     2000,
        "initial_backoff_ms":       100,
        "max_backoff_ms":           500,
        "monitor_poll_us":          100000
    },
    "scheduler": {
        "service_host": "127.0.0.1",
        "price_zones": ["SE1"],
        "timeout_ms": 1000
    }
}
EOF
}

# wait_for_health <port> <timeout_s>
# Returns 0 when /health responds 200, 1 if timeout reached.
wait_for_health() {
    local port="$1" timeout_s="${2:-10}"
    local deadline=$(($(date +%s) + timeout_s))
    while [ "$(date +%s)" -lt "$deadline" ]; do
        local code
        code=$(curl -s -o /dev/null -w "%{http_code}" \
               "http://127.0.0.1:$port/health" 2>/dev/null) || true
        [ "$code" = "200" ] && return 0
        sleep 0.2
    done
    return 1
}

# server_pid_from_log <log_file>
# Prints the PID from the last "Server spawned: PID" line.
server_pid_from_log() {
    grep "Server spawned: PID" "$1" 2>/dev/null | tail -1 | awk '{print $NF}'
}

# wait_for_new_server_pid <log_file> <old_pid> <timeout_s>
# Prints the new PID and returns 0, or returns 1 on timeout.
wait_for_new_server_pid() {
    local log="$1" old_pid="$2" timeout_s="${3:-15}"
    local deadline=$(($(date +%s) + timeout_s))
    while [ "$(date +%s)" -lt "$deadline" ]; do
        local new_pid
        new_pid=$(server_pid_from_log "$log")
        if [ -n "$new_pid" ] && [ "$new_pid" != "$old_pid" ]; then
            echo "$new_pid"
            return 0
        fi
        sleep 0.2
    done
    return 1
}

# ---------------------------------------------------------------------------
# Pre-flight checks
# ---------------------------------------------------------------------------

for bin in "$SERVER_BIN" "$WATCHDOG_BIN"; do
    if [ ! -x "$bin" ]; then
        echo "ERROR: $bin not found or not executable. Run 'make' first."
        exit 1
    fi
done

# Server always binds to port 10680 (hardcoded in http_server_initiate).
PORT=10680

echo "=== Watchdog Integration Tests ==="
echo ""

# ===========================================================================
# Test 1 — Health check does not restart a healthy server
# ===========================================================================
echo "--- Test 1: healthy server is not restarted ---"

TMPDIR_TEST=$(mktemp -d)
mkdir -p "$TMPDIR_TEST/logs" "$TMPDIR_TEST/energy_plan" "$TMPDIR_TEST/cache"

# Short health check intervals (1s) so we get several cycles in 5s.
write_watchdog_config "$TMPDIR_TEST" 1000 500 2 3000

ALL_LOG="$TMPDIR_TEST/logs/all.log"

(cd "$TMPDIR_TEST" && \
 "$WATCHDOG_BIN" -f \
     -s "$SERVER_BIN" \
     -c /bin/true \
     -l logs) >/dev/null 2>&1 &
WATCHDOG_PID=$!

if ! wait_for_health $PORT 15; then
    fail "healthy server is not restarted  (server did not come up)"
else
    INITIAL_SERVER_PID=$(server_pid_from_log "$ALL_LOG")

    # Let 5 health-check cycles complete.
    sleep 5

    # Only one "Server spawned" line should exist — no restart.
    SPAWN_COUNT=$(grep -c "Server spawned: PID" "$ALL_LOG" 2>/dev/null) || SPAWN_COUNT=0
    if [ "$SPAWN_COUNT" -eq 1 ]; then
        pass "healthy server is not restarted"
    else
        fail "healthy server is not restarted  (spawned $SPAWN_COUNT times)"
    fi
fi

cleanup
echo ""

# ===========================================================================
# Test 2 — Frozen server triggers SIGTERM → SIGKILL → restart
# ===========================================================================
echo "--- Test 2: frozen server triggers restart ---"

TMPDIR_TEST=$(mktemp -d)
mkdir -p "$TMPDIR_TEST/logs" "$TMPDIR_TEST/energy_plan" "$TMPDIR_TEST/cache"

# 1s intervals, 2 failures → SIGTERM after ~2s; 3s sigkill timeout.
write_watchdog_config "$TMPDIR_TEST" 1000 500 2 3000

ALL_LOG="$TMPDIR_TEST/logs/all.log"

(cd "$TMPDIR_TEST" && \
 "$WATCHDOG_BIN" -f \
     -s "$SERVER_BIN" \
     -c /bin/true \
     -l logs) >/dev/null 2>&1 &
WATCHDOG_PID=$!

if ! wait_for_health $PORT 15; then
    fail "frozen server triggers restart  (server did not come up)"
    cleanup
else
    INITIAL_SERVER_PID=$(server_pid_from_log "$ALL_LOG")

    if [ -z "$INITIAL_SERVER_PID" ]; then
        fail "frozen server triggers restart  (could not read server PID from log)"
        echo "[DEBUG] all.log contents:"
        cat "$ALL_LOG" 2>/dev/null || echo "(log not found)"
        cleanup
    else

    # Freeze the server — health-check reads will time out.
    kill -STOP "$INITIAL_SERVER_PID"

    # Timeline after SIGSTOP:
    #   ~1.0s  first  HC times out (500ms) → fail 1
    #   ~2.0s  second HC times out (500ms) → fail 2 → SIGTERM sent
    #   ~5.0s  sigkill_timeout_ms (3s) → SIGKILL → server dead
    #   ~5.1s  100ms backoff, spawn new server
    # Wait 10s to be safe (server_ready_wait_ms=2000).
    sleep 10

    NEW_PID=$(wait_for_new_server_pid "$ALL_LOG" "$INITIAL_SERVER_PID" 3 || echo "")

    if [ -z "$NEW_PID" ]; then
        # Unfreeze so cleanup can kill it.
        kill -CONT "$INITIAL_SERVER_PID" 2>/dev/null || true
        fail "frozen server triggers restart  (no new PID within timeout)"
        echo "[DEBUG] all.log contents:"
        cat "$ALL_LOG" 2>/dev/null || echo "(log not found)"
        cleanup
    else
        pass "frozen server triggers restart  (old=$INITIAL_SERVER_PID new=$NEW_PID)"

        # Test 2b — restarted server must serve /health.
        if wait_for_health $PORT 10; then
            pass "restarted server responds to /health"
        else
            fail "restarted server responds to /health"
        fi

        cleanup
    fi
    fi  # if [ -z "$INITIAL_SERVER_PID" ]
fi
echo ""

# ===========================================================================
# Test 3 — In-flight request receives 503 when server gets SIGTERM
#
# Requires outbound HTTPS to api.open-meteo.com to keep the request
# in WAIT_RESPONSE state long enough for SIGTERM to arrive.
# ===========================================================================
echo "--- Test 3: in-flight request is dropped on SIGTERM ---"

TMPDIR_TEST=$(mktemp -d)
mkdir -p "$TMPDIR_TEST/logs" "$TMPDIR_TEST/energy_plan" "$TMPDIR_TEST/cache"

# Minimal config — use all defaults (price_zones default is 4 zones which
# passes config_parser_validate; cache dir is empty → cache miss guaranteed).
cat > "$TMPDIR_TEST/config.json" <<'EOF'
{
    "scheduler": { "price_zones": ["SE1"] }
}
EOF

# Start the server directly (no watchdog).
(cd "$TMPDIR_TEST" && \
 "$SERVER_BIN" --log-dir logs --base-dir .) >/dev/null 2>&1 &
INITIAL_SERVER_PID=$!

if ! wait_for_health $PORT 10; then
    fail "in-flight request gets 503 on SIGTERM  (server did not come up)"
    cleanup
else
    # Fire a request with coordinates that are guaranteed to miss the empty
    # cache and go async to the external API (→ WAIT_RESPONSE state).
    CURL_OUT=$(mktemp)
    CURL_STATUS=$(mktemp)
    curl -s --max-time 30 \
        -o "$CURL_OUT" -w "%{http_code}" \
        "http://127.0.0.1:$PORT/v1/minutely?lat=0.0001&lon=0.0001" \
        > "$CURL_STATUS" 2>/dev/null &
    CURL_PID=$!

    # 150ms is enough for the server to accept + parse + enter WAIT_RESPONSE
    # (~50ms in practice) but less than a TLS handshake to a remote host
    # (~200-400ms), so SIGTERM reliably arrives while the request is in-flight.
    sleep 0.15

    # Trigger graceful shutdown — dispose() queues 503 for WAIT_RESPONSE conns.
    kill -TERM "$INITIAL_SERVER_PID"

    # Wait for curl to complete (bounded by --max-time 30).
    wait "$CURL_PID" || true
    HTTP_CODE=$(cat "$CURL_STATUS" 2>/dev/null)
    CURL_BODY=$(cat "$CURL_OUT" 2>/dev/null)
    rm -f "$CURL_OUT" "$CURL_STATUS"

    if [ "$HTTP_CODE" != "200" ]; then
        pass "in-flight request is dropped on SIGTERM  (HTTP $HTTP_CODE)"
    else
        fail "in-flight request is dropped on SIGTERM  (got 200 — server should have closed the connection)"
    fi

    INITIAL_SERVER_PID=""
    cleanup
fi
echo ""

# ===========================================================================
# Summary
# ===========================================================================
TOTAL=$((PASSED + FAILED))
if [ "$FAILED" -gt 0 ]; then
    echo "=== Results: $PASSED/$TOTAL passed, $FAILED FAILED ==="
    exit 1
else
    echo "=== Results: $PASSED/$TOTAL passed ==="
    exit 0
fi
