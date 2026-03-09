#!/bin/bash
set -e

PUID=${PUID:-1000}
PGID=${PGID:-1000}

echo "[entrypoint] Running as UID=${PUID} GID=${PGID}"

# Remap appuser to match host UID/GID so bind-mount writes work on any machine
groupmod -o -g "$PGID" appuser 2>/dev/null || true
usermod  -o -u "$PUID" appuser 2>/dev/null || true

# Fix ownership of writable dirs (read-only mounts like config.json are fine)
chown -R appuser:appuser /app/logs /app/cache /app/energy_plan

exec gosu appuser ./build/release/watchdog \
    --foreground \
    --server  ./build/release/server \
    --compute ./build/release/compute
