#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
CONFIG_PATH="${1:-config/camera_preview_demo.yaml}"

cd "${ROOT_DIR}"

echo "[RUN] camera_preview_demo ${CONFIG_PATH}"

"${ROOT_DIR}/build/camera_preview_demo" "${CONFIG_PATH}"
