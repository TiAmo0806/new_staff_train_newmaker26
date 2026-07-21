#!/bin/bash
# ============================================================
#  RoboMaster 视觉守护脚本 (看门狗)
#  用法: ./vision_daemon.sh
#
#  开机自启配置: ./setup_autostart.sh
# ============================================================

# ==================== 路径配置 ====================
PROJECT_DIR="/home/dyx/桌面/bean_sorting_framework"
EXEC_PATH="${PROJECT_DIR}/build/bean_sorting"
PROCESS_NAME="bean_sorting"
LOG_DIR="${PROJECT_DIR}/logs"

# ==================== 守护参数 ====================
CHECK_INTERVAL=2
MAX_RESTART=50
RESTART_COOLDOWN=5

# ============================================================

kill_old() {
    local pids=$(ps -ef | grep "${PROCESS_NAME}" | grep -v grep | grep -v "$0" | awk '{print $2}')
    if [ -n "$pids" ]; then
        echo "[Daemon] 清理残留进程: $pids"
        echo "$pids" | xargs kill -9 2>/dev/null
        sleep 1
    fi
}

start_vision() {
    cd "${PROJECT_DIR}" || { echo "[ERROR] 项目目录不存在"; exit 1; }
    echo "[Daemon] 启动视觉程序..."
    "${EXEC_PATH}" >> "${LOG_FILE}" 2>&1 &
    VISION_PID=$!
    echo "[Daemon] PID: ${VISION_PID}"
}

is_running() {
    if [ -n "$VISION_PID" ] && ps -ef | grep -v grep | grep -v "$0" | grep -q "${VISION_PID}"; then
        return 0
    fi
    local count=$(ps -ef | grep "${PROCESS_NAME}" | grep -v grep | grep -v "$0" | wc -l)
    [ "$count" -gt 0 ] && return 0
    return 1
}

trap_exit() {
    echo "[Daemon] 正在退出..."
    if [ -n "$VISION_PID" ] && is_running; then
        kill $VISION_PID 2>/dev/null
        sleep 1
        is_running && kill -9 $VISION_PID 2>/dev/null
    fi
    echo "[Daemon] 已退出"
    exit 0
}

trap trap_exit SIGINT SIGTERM SIGHUP

# ============================================================

mkdir -p "${LOG_DIR}"
LOG_FILE="${LOG_DIR}/vision_$(date +%Y%m%d_%H%M%S).log"
ls -t "${LOG_DIR}"/vision_*.log 2>/dev/null | tail -n +6 | xargs rm -f 2>/dev/null

kill_old

echo "============================================"
echo "  RoboMaster Vision Daemon"
echo "  进程: ${PROCESS_NAME}"
echo "  日志: ${LOG_FILE}"
echo "============================================"

restart_count=0

while true; do
    if is_running; then
        sleep ${CHECK_INTERVAL}
        continue
    fi

    restart_count=$((restart_count + 1))
    [ ${restart_count} -gt ${MAX_RESTART} ] && { echo "[Daemon] 超过最大重启次数"; exit 1; }

    echo "[Daemon] $(date '+%H:%M:%S') 进程不在, 第 ${restart_count} 次重启"
    sleep ${RESTART_COOLDOWN}

    kill_old
    start_vision
    sleep 2
done
