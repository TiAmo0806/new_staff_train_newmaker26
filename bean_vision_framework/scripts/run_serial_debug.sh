#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
LOG_DIR="${ROOT_DIR}/logs"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
LOG_FILE="${LOG_DIR}/serial_debug_${TIMESTAMP}.log"

mkdir -p "${LOG_DIR}"
cd "${ROOT_DIR}"

echo "[RUN] serial_debug_demo $*"
echo "[LOG] ${LOG_FILE}"

"${ROOT_DIR}/build/serial_debug_demo" "$@" 2>&1 | tee "${LOG_FILE}"
