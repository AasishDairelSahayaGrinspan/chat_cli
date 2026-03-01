#!/bin/bash
# Build script for the chat application

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
BUILD_TYPE="${1:-Debug}"

echo "=== Chat CLI Build Script ==="
echo "Build type: ${BUILD_TYPE}"
echo ""

# Check dependencies
echo "Checking dependencies..."

check_dependency() {
    if ! command -v "$1" &> /dev/null; then
        echo "Error: $1 is required but not installed."
        exit 1
    fi
}

check_dependency cmake
check_dependency g++
check_dependency pkg-config

# Check for required libraries
if ! pkg-config --exists openssl 2>/dev/null; then
    echo "Warning: OpenSSL development headers may not be installed"
    echo "  Install with: sudo apt-get install libssl-dev"
fi

if ! pkg-config --exists sqlite3 2>/dev/null; then
    echo "Warning: SQLite3 development headers may not be installed"
    echo "  Install with: sudo apt-get install libsqlite3-dev"
fi

if ! pkg-config --exists libsodium 2>/dev/null; then
    echo "Warning: libsodium development headers may not be installed"
    echo "  Install with: sudo apt-get install libsodium-dev"
fi

echo "Dependencies OK"
echo ""

# Create build directory
echo "Creating build directory..."
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Configure
echo "Configuring with CMake..."
cmake .. -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
         -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Build
echo ""
echo "Building..."
make -j$(nproc)

echo ""
echo "=== Build Complete ==="
echo ""
echo "Executables:"
if [ -f "${BUILD_DIR}/server/chat_server" ]; then
    echo "  Server: ${BUILD_DIR}/server/chat_server"
else
    echo "  Server: Not built (check for errors)"
fi

if [ -f "${BUILD_DIR}/client/chat_client" ]; then
    echo "  Client: ${BUILD_DIR}/client/chat_client"
else
    echo "  Client: Not built (check for errors)"
fi

echo ""
echo "Next steps:"
echo "  1. Generate TLS certificates: cd docker/certs && ./generate.sh"
echo "  2. Run server: ./build/server/chat_server"
echo "  3. Run client: ./build/client/chat_client localhost 8443"

