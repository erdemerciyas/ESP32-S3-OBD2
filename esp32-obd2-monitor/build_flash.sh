#!/bin/bash
# ESP32-S3 OBD2 Monitor - Build and Flash Script
# Usage: ./build_flash.sh [build|flash|monitor|clean|all]

set -e

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$PROJECT_DIR"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

print_step() {
    echo -e "${GREEN}==>${NC} $1"
}

print_error() {
    echo -e "${RED}ERROR:${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}WARNING:${NC} $1"
}

check_idf() {
    if [ -z "$IDF_PATH" ]; then
        print_error "ESP-IDF environment not set. Run: source \$IDF_PATH/export.sh"
        exit 1
    fi
}

do_clean() {
    print_step "Cleaning build artifacts..."
    idf.py clean
    rm -rf build dependencies.lock
}

do_build() {
    check_idf
    print_step "Setting target to ESP32-S3..."
    idf.py set-target esp32s3

    print_step "Building project..."
    idf.py build
    print_step "Build complete!"
}

do_flash() {
    check_idf
    print_step "Flashing to ESP32-S3..."
    idf.py -p "${PORT:-/dev/ttyUSB0}" flash
}

do_monitor() {
    check_idf
    print_step "Starting serial monitor..."
    idf.py -p "${PORT:-/dev/ttyUSB0}" monitor
}

do_all() {
    do_clean
    do_build
    do_flash
    do_monitor
}

case "${1:-build}" in
    clean)
        do_clean
        ;;
    build)
        do_build
        ;;
    flash)
        do_flash
        ;;
    monitor)
        do_monitor
        ;;
    all)
        do_all
        ;;
    *)
        echo "Usage: $0 [build|flash|monitor|clean|all]"
        echo ""
        echo "Environment variables:"
        echo "  PORT    Serial port (default: /dev/ttyUSB0)"
        exit 1
        ;;
esac
