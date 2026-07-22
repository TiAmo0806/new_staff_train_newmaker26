#!/bin/bash
# ============================================================
# 迈德威视 + OpenVINO YOLO11 实时检测 — 自启动脚本
#
# 用法:
#   ./start.sh                          # 默认模型 + 配置
#   ./start.sh ../model3/best5.xml       # 指定模型路径
#   ./start.sh ../model3/best.xml ../config.yaml
#
# CMakeLists.txt → 可执行文件: build/mvs_openvino_demo
# 配合 systemd user service 实现开机自启动
# ============================================================

set -e

# ---- 脚本所在目录（绝对路径，支持软链接） ----
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

# ---- 默认路径（相对于项目根目录） ----
MODEL_PATH="${1:-${SCRIPT_DIR}/model3/best5.xml}"
CONFIG_PATH="${2:-${SCRIPT_DIR}/config.yaml}"

# ---- 日志文件 ----
LOG_FILE="${SCRIPT_DIR}/start.log"

# ============================================================
# 环境变量 — 根据 OpenVINO 安装方式自动检测
# ============================================================

# 方式 1: 压缩包安装（树莓派常见）
if [ -d "$HOME/openvino_runtime/lib" ]; then
    export LD_LIBRARY_PATH="$HOME/openvino_runtime/lib:$LD_LIBRARY_PATH"
fi

# 方式 2: pip 安装
for PY_VER in python3.10 python3.11 python3.12; do
    OV_LIBS="$HOME/.local/lib/$PY_VER/site-packages/openvino/libs"
    if [ -d "$OV_LIBS" ]; then
        export LD_LIBRARY_PATH="$OV_LIBS:$LD_LIBRARY_PATH"
        break
    fi
done

# 方式 3: 自动搜索
if [ -z "$LD_LIBRARY_PATH" ]; then
    OV_LIB=$(find /home /opt -name "libopenvino.so*" -type f 2>/dev/null | head -1)
    if [ -n "$OV_LIB" ]; then
        export LD_LIBRARY_PATH="$(dirname "$OV_LIB"):$LD_LIBRARY_PATH"
    fi
fi

# ============================================================
# 检查可执行文件
# ============================================================
EXE="${BUILD_DIR}/mvs_openvino_demo"
if [ ! -f "$EXE" ]; then
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] ERROR: 找不到 ${EXE}" | tee -a "$LOG_FILE"
    echo "请先运行 ./build.sh 编译项目" | tee -a "$LOG_FILE"
    exit 1
fi

# ============================================================
# 启动
# ============================================================
echo "[$(date '+%Y-%m-%d %H:%M:%S')] 程序启动" >> "$LOG_FILE"
echo "  EXE:    ${EXE}"       >> "$LOG_FILE"
echo "  MODEL:  ${MODEL_PATH}" >> "$LOG_FILE"
echo "  CONFIG: ${CONFIG_PATH}" >> "$LOG_FILE"
echo "  DISPLAY: ${DISPLAY:-未设置}" >> "$LOG_FILE"

cd "${BUILD_DIR}"
"${EXE}" "${MODEL_PATH}" "${CONFIG_PATH}"
EXIT_CODE=$?

echo "[$(date '+%Y-%m-%d %H:%M:%S')] 程序退出，退出码=${EXIT_CODE}" >> "$LOG_FILE"
exit ${EXIT_CODE}
