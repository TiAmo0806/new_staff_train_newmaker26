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

# ---- 脚本所在目录 ----
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

# 拷贝模型文件到 build 目录
echo ""
echo "[3/3] 拷贝模型文件..."
MODEL_SRC="${PROJECT_DIR}/model3"
if [ -d "$MODEL_SRC" ]; then
    cp -u "$MODEL_SRC"/*.xml "$MODEL_SRC"/*.bin "${BUILD_DIR}/" 2>/dev/null || true
    echo "  模型文件已拷贝到 build/"
else
    echo "  提示: 未找到 model3/ 目录，请手动拷贝模型文件到 build/"
fi

echo ""
echo "===== 编译完成 ====="
echo "运行: cd ${BUILD_DIR} && ./mvs_openvino_demo ../model3/best5.xml ../config.yaml"
