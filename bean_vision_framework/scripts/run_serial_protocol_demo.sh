#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
LOG_DIR="${ROOT_DIR}/logs"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
LOG_FILE="${LOG_DIR}/serial_protocol_demo_${TIMESTAMP}.log"

mkdir -p "${LOG_DIR}"
cd "${ROOT_DIR}"

echo "[RUN] serial_protocol_demo $*"
echo "[LOG] ${LOG_FILE}"

stdbuf -oL -eL "${ROOT_DIR}/build/serial_protocol_demo" "$@" 2>&1 | tee "${LOG_FILE}"
