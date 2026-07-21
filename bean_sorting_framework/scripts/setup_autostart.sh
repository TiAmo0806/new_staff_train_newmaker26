#!/bin/bash
# ============================================================
#  RoboMaster 视觉自启动脚本 (配置 + 守护 合一)
#
#  首次运行:  ./setup_autostart.sh install   配置自动登录 + 开机自启
#  之后每次开机自动运行守护模式
#  手动守护: ./setup_autostart.sh daemon     仅启动看门狗
# ============================================================

set -e

PROJECT_DIR="/home/dyx/桌面/bean_sorting_framework"
EXEC_PATH="${PROJECT_DIR}/build/bean_sorting"
PROCESS_NAME="bean_sorting"
LOG_DIR="${PROJECT_DIR}/logs"
AUTOSTART_DIR="${HOME}/.config/autostart"
DESKTOP_FILE="${AUTOSTART_DIR}/vision_daemon.desktop"
SELF_PATH="$(realpath "$0")"

CHECK_INTERVAL=2
MAX_RESTART=50
RESTART_COOLDOWN=5

# ============================================================
#  install: 一键配置开机自动登录 + 创建自启条目
# ============================================================
do_install() {
    echo "============================================"
    echo "  RoboMaster 视觉自启动配置"
    echo "============================================"

    # 1. 关锁屏
    echo ""
    echo "[1] 关闭锁屏..."
    gsettings set org.gnome.desktop.screensaver lock-enabled false 2>/dev/null || true
    gsettings set org.gnome.desktop.session idle-delay 0 2>/dev/null || true
    echo "  [OK]"

    # 2. 无密码自动登录
    echo "[2] 配置开机自动登录 (用户: ${USER})..."
    if [ -f /etc/gdm3/custom.conf ]; then
        sudo sed -i 's/^#\?\s*AutomaticLoginEnable\s*=.*/AutomaticLoginEnable = true/' /etc/gdm3/custom.conf
        sudo sed -i "s/^#\?\s*AutomaticLogin\s*=.*/AutomaticLogin = ${USER}/" /etc/gdm3/custom.conf
        grep -q "^AutomaticLoginEnable" /etc/gdm3/custom.conf || \
            echo "AutomaticLoginEnable = true" | sudo tee -a /etc/gdm3/custom.conf > /dev/null
        grep "^AutomaticLogin[^E]" /etc/gdm3/custom.conf > /dev/null 2>&1 || \
            echo "AutomaticLogin = ${USER}" | sudo tee -a /etc/gdm3/custom.conf > /dev/null
        echo "  [OK] GDM 自动登录已配置"
    else
        echo "  [跳过] 非 GDM, 请手动设置自动登录"
    fi

    # 3. 脚本本身加执行权限
    echo "[3] 创建自启条目..."
    chmod +x "${SELF_PATH}"
    mkdir -p "${AUTOSTART_DIR}"
    cat > "${DESKTOP_FILE}" << EOF
[Desktop Entry]
Type=Application
Name=RoboMaster Vision
Exec=${SELF_PATH} daemon
Terminal=true
X-GNOME-Autostart-enabled=true
StartupNotify=false
EOF
    echo "  [OK] ${DESKTOP_FILE}"

    echo ""
    echo "============================================"
    echo "  配置完成!  重启后自动生效:"
    echo "    开机 → 自动登录 → 终端弹出 → 守护启动"
    echo "============================================"
}

# ============================================================
#  daemon: 看门狗模式 (死循环监控进程存活)
# ============================================================
do_daemon() {
    mkdir -p "${LOG_DIR}"
    LOG_FILE="${LOG_DIR}/vision_$(date +%Y%m%d_%H%M%S).log"
    ls -t "${LOG_DIR}"/vision_*.log 2>/dev/null | tail -n +6 | xargs rm -f 2>/dev/null

    # 杀残留
    ps -ef | grep "${PROCESS_NAME}" | grep -v grep | grep -v "$0" | awk '{print $2}' | xargs kill -9 2>/dev/null || true
    sleep 1

    echo "============================================"
    echo "  RoboMaster Vision Daemon"
    echo "  进程: ${PROCESS_NAME}"
    echo "  日志: ${LOG_FILE}"
    echo "============================================"

    # 启动
    cd "${PROJECT_DIR}"
    "${EXEC_PATH}" >> "${LOG_FILE}" 2>&1 &
    VISION_PID=$!
    echo "[Daemon] PID: ${VISION_PID}"

    restart_count=0
    while true; do
        sleep ${CHECK_INTERVAL}

        # 检查进程是否存活
        if ps -ef | grep -v grep | grep -v "$0" | grep -q "${VISION_PID}"; then
            continue
        fi
        if [ "$(ps -ef | grep "${PROCESS_NAME}" | grep -v grep | grep -v "$0" | wc -l)" -gt 0 ]; then
            continue
        fi

        # 挂了, 重启
        restart_count=$((restart_count + 1))
        if [ ${restart_count} -gt ${MAX_RESTART} ]; then
            echo "[Daemon] 超过最大重启次数, 退出"
            exit 1
        fi

        echo "[Daemon] $(date '+%H:%M:%S') 进程不在, 第 ${restart_count} 次重启"
        sleep ${RESTART_COOLDOWN}

        ps -ef | grep "${PROCESS_NAME}" | grep -v grep | grep -v "$0" | awk '{print $2}' | xargs kill -9 2>/dev/null || true
        sleep 1
        "${EXEC_PATH}" >> "${LOG_FILE}" 2>&1 &
        VISION_PID=$!
        echo "[Daemon] 新 PID: ${VISION_PID}"
        sleep 2
    done
}

# ============================================================
#  main
# ============================================================
case "${1}" in
    install) do_install ;;
    daemon)  do_daemon ;;
    *)
        echo "用法: $0 {install|daemon}"
        echo "  install  一键配置开机自动登录 + 启动项"
        echo "  daemon   启动守卫进程"
        exit 1
        ;;
esac
