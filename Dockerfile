# ----------------------------
# Stage 1 — Build
# ----------------------------
FROM ubuntu:22.04 AS builder
ENV DEBIAN_FRONTEND=noninteractive
WORKDIR /app

RUN apt-get update && apt-get install -y \
    build-essential \
    git \
    libcurl4-openssl-dev \
    libmbedtls-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY . .
RUN git submodule update --init --recursive
RUN make BUILD_MODE=release all

# ----------------------------
# Stage 2 — Runtime
# ----------------------------
FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
WORKDIR /app

RUN apt-get update && apt-get install -y \
    libcurl4 \
    libmbedtls14 \
    libmbedx509-1 \
    libmbedcrypto7 \
    ca-certificates \
    gosu \
    && rm -rf /var/lib/apt/lists/*

# Create appuser — UID/GID will be remapped at runtime by entrypoint.sh
RUN useradd -r -s /usr/sbin/nologin appuser

COPY --from=builder /app/build/release ./build/release
COPY data/ ./data
COPY entrypoint.sh ./entrypoint.sh

# Pre-create writable dirs and own everything as appuser
# entrypoint.sh will re-chown to the host UID/GID before exec
RUN mkdir -p /app/logs /app/cache /app/energy_plan \
    && chown -R appuser:appuser /app \
    && chmod +x ./entrypoint.sh

# Entrypoint runs as root so it can remap UIDs, then drops to appuser via gosu
ENTRYPOINT ["./entrypoint.sh"]
