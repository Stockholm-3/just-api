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
    && rm -rf /var/lib/apt/lists/*

RUN useradd -r -s /usr/sbin/nologin appuser

RUN mkdir -p /app/logs && chown -R appuser:appuser /app

COPY --from=builder /app/build/release ./build/release
COPY data/ ./data

USER appuser

ENTRYPOINT ["./build/release/watchdog"]
CMD ["--foreground", "--server", "./build/release/server", "--compute", "./build/release/compute"]
