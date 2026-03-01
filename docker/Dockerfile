# Multi-stage build for production
FROM ubuntu:22.04 AS builder

# Prevent interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libboost-system-dev \
    libssl-dev \
    libsqlite3-dev \
    libsodium-dev \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

# Copy source
WORKDIR /app
COPY . .

# Build
RUN mkdir -p build && cd build \
    && cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_CLIENT=OFF .. \
    && make -j$(nproc)

# Runtime image
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    libboost-system1.74.0 \
    libssl3 \
    libsqlite3-0 \
    libsodium23 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Create non-root user
RUN useradd -m -s /bin/bash chatserver

# Copy binary
COPY --from=builder /app/build/server/chat_server /usr/local/bin/

# Create directories
RUN mkdir -p /app/data /app/certs /app/config && \
    chown -R chatserver:chatserver /app

# Copy default config
COPY docker/config.json /app/config/

WORKDIR /app
USER chatserver

# Expose ports
EXPOSE 8443 8080

# Health check
HEALTHCHECK --interval=30s --timeout=5s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:8080/healthz || exit 1

# Default command
CMD ["chat_server", "/app/config/config.json"]

