#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
LOG_DIR="${ROOT_DIR}/logs"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
LOG_FILE="${LOG_DIR}/debug_image_real_serial_${TIMESTAMP}.log"
CONFIG_PATH="${1:-config/debug_image_real_serial.yaml}"

mkdir -p "${LOG_DIR}"
cd "${ROOT_DIR}"

echo "[RUN] bean_vision_framework ${CONFIG_PATH}"
echo "[LOG] ${LOG_FILE}"

"${ROOT_DIR}/build/bean_vision_framework" "${CONFIG_PATH}" 2>&1 | tee "${LOG_FILE}"
