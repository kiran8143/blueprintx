# Multi-stage build for Drogon Blueprint Framework
# Stage 1: Build with all dependencies
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
ENV VCPKG_FORCE_SYSTEM_BINARIES=1

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential cmake git curl zip unzip tar pkg-config \
    libssl-dev libc-ares-dev libpq-dev default-libmysqlclient-dev \
    libsqlite3-dev uuid-dev libjsoncpp-dev bison flex \
    ninja-build python3 \
    && rm -rf /var/lib/apt/lists/*

# Install vcpkg
RUN git clone https://github.com/microsoft/vcpkg.git /opt/vcpkg \
    && /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics
ENV VCPKG_ROOT=/opt/vcpkg
ENV PATH="${VCPKG_ROOT}:${PATH}"

# Copy dependency manifest first (for Docker layer caching)
WORKDIR /app
COPY vcpkg.json CMakeLists.txt CMakePresets.json ./

# Copy source code
COPY src/ src/
COPY main.cpp config.json ./

# Build with Release preset
RUN cmake --preset=release -B build \
    && cmake --build build --parallel $(nproc)

# Stage 2: Minimal runtime image
FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

# Install only runtime libraries
RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 libc-ares2 libpq5 libmysqlclient21 \
    libsqlite3-0 libjsoncpp25 libuuid1 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Create non-root user
RUN useradd -r -s /bin/false appuser

WORKDIR /app

# Copy built binary
COPY --from=builder /app/build/drogon_blueprint .

# Copy configuration files
COPY config.json .
COPY .env.example .env

# Set permissions
RUN chown -R appuser:appuser /app

USER appuser

EXPOSE 8080

# Health check for container orchestration
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD curl -f http://localhost:8080/health || exit 1

ENTRYPOINT ["./drogon_blueprint"]
