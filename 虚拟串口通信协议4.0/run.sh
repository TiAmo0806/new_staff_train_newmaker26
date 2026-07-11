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

TMPDIR="${TMPDIR:-/tmp}"
VISION_PORT="${TMPDIR}/ttyV0"
CTRL_PORT="${TMPDIR}/ttyV1"

# 清理上次残留的 socat 进程
pkill -f "socat.*ttyV0.*ttyV1" 2>/dev/null || true
rm -f "$VISION_PORT" "$CTRL_PORT"

# 用 socat 创建虚拟串口对（双向连通）
echo "=== 虚拟串口: $VISION_PORT <-> $CTRL_PORT ==="
socat -d -d pty,raw,echo=0,link="$VISION_PORT",perm=0666 pty,raw,echo=0,link="$CTRL_PORT",perm=0666 &
SOCAT_PID=$!

# 等待设备创建
for i in $(seq 1 10); do
  if [ -e "$VISION_PORT" ] && [ -e "$CTRL_PORT" ]; then break; fi
  sleep 0.5
done

if [ ! -e "$VISION_PORT" ] || [ ! -e "$CTRL_PORT" ]; then
  echo "[错误] 虚拟串口创建失败"
  kill $SOCAT_PID 2>/dev/null
  exit 1
fi

cleanup() {
  echo "=== 清理 ==="
  kill $SOCAT_PID 2>/dev/null || true
  rm -f "$VISION_PORT" "$CTRL_PORT"
}
trap cleanup EXIT

echo "=== VisionNode ($VISION_PORT) & ControlNode ($CTRL_PORT) ==="
./build/VisionNode "$VISION_PORT" &
VISION_PID=$!
./build/ControlNode "$CTRL_PORT" &
CTRL_PID=$!

wait $VISION_PID $CTRL_PID
echo "=== 退出 ==="
