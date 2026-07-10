#!/bin/bash
# ============================================================
# 一键编译脚本
# 用法: ./build.sh [OpenVINO_DIR]
#
# 示例:
#   ./build.sh                                    # 自动查找 OpenVINO
#   ./build.sh /opt/intel/openvino/runtime/cmake   # 指定路径
# ============================================================

set -e

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"

echo "===== 迈德威视 + OpenVINO YOLO11 实时检测 ====="
echo "项目目录: ${PROJECT_DIR}"
echo "编译目录: ${BUILD_DIR}"

# 创建 build 目录
mkdir -p "${BUILD_DIR}"

# 构建 CMake 参数
CMAKE_ARGS=""
if [ -n "$1" ]; then
    CMAKE_ARGS="-DOpenVINO_DIR=$1"
    echo "OpenVINO_DIR: $1"
else
    echo "OpenVINO_DIR: 自动查找"
fi

# CMake 配置
echo ""
echo "[1/3] CMake 配置..."
cd "${BUILD_DIR}"
cmake .. ${CMAKE_ARGS}

# 编译
echo ""
echo "[2/3] 编译..."
make -j$(nproc)

# 检查模型文件
echo ""
echo "[3/3] 检查模型文件..."
MODEL_DIR="${BUILD_DIR}/best_openvino_model"
if [ -f "${MODEL_DIR}/best.xml" ] && [ -f "${MODEL_DIR}/best.bin" ]; then
    echo "  模型文件已就绪: ${MODEL_DIR}/"
else
    echo "  提示: 请将 best.xml 和 best.bin 复制到以下目录:"
    echo "    ${MODEL_DIR}/"
    echo "  或运行时通过参数指定模型路径"
fi

echo ""
echo "===== 编译完成 ====="
echo "运行: cd ${BUILD_DIR} && ./mvs_openvino_demo [模型路径]"
echo "可选模型路径默认: ${BUILD_DIR}/best_openvino_model/best.xml"
