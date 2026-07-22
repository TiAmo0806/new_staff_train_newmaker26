#!/bin/bash
# 兼容旧命令；实际安装工作由setup_autostart.sh完成。
set -e
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
exec "$SCRIPT_DIR/setup_autostart.sh" install

