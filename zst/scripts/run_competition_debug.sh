#!/bin/bash
set -e

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"

"$SCRIPT_DIR/vision_daemon.sh" stop 2>/dev/null || true
if systemctl is-active --quiet logistics-vision.service 2>/dev/null; then
    sudo systemctl stop logistics-vision.service
fi

echo "以competition流程启动，临时开启图像窗口和终端实时识别。"
echo "窗口按Q或ESC退出；退出后可执行：$SCRIPT_DIR/setup_autostart.sh daemon"

exec "$SCRIPT_DIR/start_vision.sh" --show-window --terminal-detections \
    --keep-progress-on-start
