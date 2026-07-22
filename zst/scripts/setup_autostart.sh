#!/bin/bash
# GNOME桌面自动登录 + 视觉程序开机自启动管理器。
# 用法：setup_autostart.sh {install|daemon|enable|disable|status|uninstall}

set -e

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
PROJECT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd -P)"
SELF_PATH="$SCRIPT_DIR/setup_autostart.sh"
DAEMON_PATH="$SCRIPT_DIR/vision_daemon.sh"
RUN_USER="$(id -un)"
USER_HOME="$(getent passwd "$RUN_USER" | cut -d: -f6)"
AUTOSTART_DIR="$USER_HOME/.config/autostart"
DESKTOP_FILE="$AUTOSTART_DIR/newmaker-vision.desktop"
DISABLED_FILE="$DESKTOP_FILE.disabled"
OLD_SERVICE="logistics-vision.service"

find_openvino_setup() {
    if [ -n "${OPENVINO_SETUPVARS:-}" ] && [ -f "$OPENVINO_SETUPVARS" ]; then
        printf '%s\n' "$OPENVINO_SETUPVARS"
        return 0
    fi

    for SETUP_FILE in \
        "$USER_HOME/openvino/setupvars.sh" \
        "$PROJECT_DIR/../openvino/setupvars.sh" \
        /opt/intel/openvino/setupvars.sh \
        /opt/intel/openvino_*/setupvars.sh
    do
        if [ -f "$SETUP_FILE" ]; then
            printf '%s\n' "$SETUP_FILE"
            return 0
        fi
    done
    return 1
}

find_mindvision_root() {
    if [ -n "${MINDVISION_ROOT:-}" ] && [ -f "$MINDVISION_ROOT/include/CameraApi.h" ]; then
        printf '%s\n' "$MINDVISION_ROOT"
        return 0
    fi

    shopt -s nullglob
    local candidates=("$PROJECT_DIR"/../linuxSDK* "$USER_HOME"/linuxSDK*)
    shopt -u nullglob
    local candidate
    for candidate in "${candidates[@]}"; do
        if [ -f "$candidate/include/CameraApi.h" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    return 1
}

compile_project() {
    local openvino_setup
    local mindvision_root

    openvino_setup="$(find_openvino_setup)" || {
        echo "错误：找不到OpenVINO setupvars.sh。" >&2
        echo "你的常见路径应为：$USER_HOME/openvino/setupvars.sh" >&2
        echo "也可以先执行：export OPENVINO_SETUPVARS=/实际路径/setupvars.sh" >&2
        exit 1
    }
    # shellcheck disable=SC1090
    source "$openvino_setup"

    mindvision_root="$(find_mindvision_root)" || {
        echo "错误：找不到迈德威视SDK的include/CameraApi.h。" >&2
        echo "请先执行：export MINDVISION_ROOT=/实际SDK根目录" >&2
        exit 1
    }

    echo "OpenVINO：$openvino_setup"
    echo "MindVision：$mindvision_root"
    echo "正在编译：$PROJECT_DIR/build-ubuntu/logistics_vision"

    cmake -S "$PROJECT_DIR" -B "$PROJECT_DIR/build-ubuntu" \
        -DMINDVISION_ROOT="$mindvision_root"
    cmake --build "$PROJECT_DIR/build-ubuntu" -j"$(nproc)"
}

configure_gdm_autologin() {
    local gdm_config="/etc/gdm3/custom.conf"
    if [ ! -f "$gdm_config" ]; then
        echo "[跳过] 未发现GDM配置，请在Ubuntu设置中手动开启自动登录。"
        return 0
    fi

    sudo cp -n "$gdm_config" "${gdm_config}.newmaker-backup" || true

    if grep -Eq '^[#[:space:]]*AutomaticLoginEnable[[:space:]]*=' "$gdm_config"; then
        sudo sed -i -E \
            's/^[#[:space:]]*AutomaticLoginEnable[[:space:]]*=.*/AutomaticLoginEnable = true/' \
            "$gdm_config"
    else
        sudo sed -i '/^\[daemon\]/a AutomaticLoginEnable = true' "$gdm_config"
    fi

    if grep -Eq '^[#[:space:]]*AutomaticLogin[[:space:]]*=' "$gdm_config"; then
        sudo sed -i -E \
            "s/^[#[:space:]]*AutomaticLogin[[:space:]]*=.*/AutomaticLogin = $RUN_USER/" \
            "$gdm_config"
    else
        sudo sed -i "/^\[daemon\]/a AutomaticLogin = $RUN_USER" "$gdm_config"
    fi
    echo "[OK] GDM自动登录用户：$RUN_USER"
}

disable_old_system_service() {
    if systemctl list-unit-files --type=service 2>/dev/null | grep -q "^${OLD_SERVICE}"; then
        echo "正在停用旧systemd视觉服务，避免重复占用相机和串口……"
        sudo systemctl disable --now "$OLD_SERVICE" || true
    fi
}

do_install() {
    if [ "$(uname -s)" != "Linux" ]; then
        echo "错误：只能在Ubuntu/Linux中安装。" >&2
        exit 1
    fi
    if [ "$(id -u)" -eq 0 ]; then
        echo "请使用普通桌面用户运行，不要使用sudo直接启动整个脚本。" >&2
        exit 1
    fi

    for command in cmake c++ systemctl gsettings; do
        if ! command -v "$command" >/dev/null 2>&1; then
            echo "错误：缺少命令：$command" >&2
            echo "请先安装：sudo apt install build-essential cmake libopencv-dev libyaml-cpp-dev" >&2
            exit 1
        fi
    done

    chmod +x "$SELF_PATH" "$DAEMON_PATH" \
        "$SCRIPT_DIR/start_vision.sh" "$SCRIPT_DIR/run_competition_debug.sh"

    compile_project
    disable_old_system_service

    echo "正在关闭桌面锁屏和空闲锁定……"
    gsettings set org.gnome.desktop.screensaver lock-enabled false || true
    gsettings set org.gnome.desktop.session idle-delay 0 || true

    echo "即将开启无密码自动登录；配置备份保存在custom.conf.newmaker-backup。"
    configure_gdm_autologin

    mkdir -p "$AUTOSTART_DIR"
    cat > "$DESKTOP_FILE" <<EOF
[Desktop Entry]
Version=1.0
Type=Application
Name=NewMaker Vision
Comment=Start logistics vision with watchdog after GNOME login
Exec="$SELF_PATH" daemon
Path=$PROJECT_DIR
Terminal=true
X-GNOME-Autostart-enabled=true
StartupNotify=false
X-GNOME-Autostart-Delay=3
EOF
    rm -f "$DISABLED_FILE"

    echo
    echo "安装完成：开机 -> 自动登录 -> 弹出终端 -> 启动比赛视觉。"
    echo "启动项：$DESKTOP_FILE"
    echo "重启测试：sudo reboot"
}

do_daemon() {
    exec "$DAEMON_PATH" run
}

do_disable() {
    "$DAEMON_PATH" stop || true
    if [ -f "$DESKTOP_FILE" ]; then
        mv "$DESKTOP_FILE" "$DISABLED_FILE"
    fi
    echo "已关闭视觉开机自启动。"
}

do_enable() {
    mkdir -p "$AUTOSTART_DIR"
    if [ -f "$DISABLED_FILE" ]; then
        mv "$DISABLED_FILE" "$DESKTOP_FILE"
    elif [ ! -f "$DESKTOP_FILE" ]; then
        echo "尚未安装，请先执行：$SELF_PATH install" >&2
        exit 1
    fi
    echo "已开启视觉开机自启动，下次登录桌面生效。"
}

do_status() {
    if [ -f "$DESKTOP_FILE" ]; then
        echo "开机自启动：开启"
    elif [ -f "$DISABLED_FILE" ]; then
        echo "开机自启动：关闭"
    else
        echo "开机自启动：未安装"
    fi
    "$DAEMON_PATH" status || true
}

do_uninstall() {
    "$DAEMON_PATH" stop || true
    rm -f "$DESKTOP_FILE" "$DISABLED_FILE"
    disable_old_system_service
    echo "已删除视觉桌面启动项。"
    echo "为避免误改系统，GDM自动登录配置没有自动还原。"
}

case "${1:-}" in
    install)   do_install ;;
    daemon)    do_daemon ;;
    enable)    do_enable ;;
    disable)   do_disable ;;
    status)    do_status ;;
    uninstall) do_uninstall ;;
    *)
        echo "用法：$0 {install|daemon|enable|disable|status|uninstall}"
        exit 1
        ;;
esac

