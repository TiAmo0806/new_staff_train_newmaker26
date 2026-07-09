#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

cmake -S "${PROJECT_DIR}" -B "${PROJECT_DIR}/build"
cmake --build "${PROJECT_DIR}/build" -j4
"${PROJECT_DIR}/build/bean_vision_framework" "${PROJECT_DIR}/config/app.yaml"
