#!/usr/bin/env bash
# ESP32-S3 OBD2 Monitor — build / flash helper (Linux / macOS)
# Usage: ./build_flash.sh all [/dev/ttyUSB0]
# See UPLOAD.md

set -euo pipefail

ACTION="${1:-build}"
PORT="${2:-/dev/ttyUSB0}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if ! command -v idf.py >/dev/null 2>&1; then
    if [[ -n "${IDF_PATH:-}" && -f "${IDF_PATH}/export.sh" ]]; then
        # shellcheck source=/dev/null
        . "${IDF_PATH}/export.sh"
    else
        echo "idf.py not found. Source ESP-IDF export.sh first (see UPLOAD.md)." >&2
        exit 1
    fi
fi

cd "${SCRIPT_DIR}"

case "${ACTION}" in
    build)
        idf.py build
        ;;
    flash)
        idf.py -p "${PORT}" flash
        ;;
    monitor)
        idf.py -p "${PORT}" monitor
        ;;
    all)
        idf.py build
        idf.py -p "${PORT}" flash monitor
        ;;
    reconfigure)
        rm -rf build sdkconfig
        idf.py set-target esp32s3
        idf.py build
        ;;
    *)
        echo "Usage: $0 {build|flash|monitor|all|reconfigure} [port]" >&2
        exit 1
        ;;
esac
