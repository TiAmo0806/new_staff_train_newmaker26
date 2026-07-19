#!/bin/bash
set -e

cd "$(dirname "$0")"

# 编译
if [ ! -d build ]; then
  mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)
  cd ..
else
  cd build && make -j$(nproc) && cd ..
fi

echo "=== 直接连真实串口（串口名称在 config.yaml 配置） ==="
./build/VisionNode
