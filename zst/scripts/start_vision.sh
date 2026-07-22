#!/bin/bash
set -e

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
PROJECT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd -P)"
CONFIG_FILE="${VISION_CONFIG:-$PROJECT_DIR/config/vision.yaml}"
RUN_USER="$(id -un)"
USER_HOME="$(getent passwd "$RUN_USER" | cut -d: -f6)"

# systemd不会读取用户的.bashrc，因此在这里加载OpenVINO环境。
if [ -n "${OPENVINO_SETUPVARS:-}" ] && [ -f "$OPENVINO_SETUPVARS" ]; then
    # shellcheck disable=SC1090
    source "$OPENVINO_SETUPVARS"
else
    for SETUP_FILE in \
        "$USER_HOME/openvino/setupvars.sh" \
        "$PROJECT_DIR/../openvino/setupvars.sh" \
        /opt/intel/openvino/setupvars.sh \
        /opt/intel/openvino_*/setupvars.sh
    do
        if [ -f "$SETUP_FILE" ]; then
            # shellcheck disable=SC1090
            source "$SETUP_FILE"
            break
        fi
    done
fi

if [ -x "$PROJECT_DIR/build-ubuntu/logistics_vision" ]; then
    PROGRAM="$PROJECT_DIR/build-ubuntu/logistics_vision"
elif [ -x "$PROJECT_DIR/build/logistics_vision" ]; then
    PROGRAM="$PROJECT_DIR/build/logistics_vision"
else
    echo "错误：找不到编译后的 logistics_vision。" >&2
    echo "请先运行：$SCRIPT_DIR/install_autostart.sh" >&2
    exit 1
fi

if [ ! -f "$CONFIG_FILE" ]; then
    echo "错误：找不到配置文件：$CONFIG_FILE" >&2
    exit 1
fi

cd "$PROJECT_DIR"
exec "$PROGRAM" "$CONFIG_FILE" "$@"
