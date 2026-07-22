#!/bin/bash
# 视觉程序看门狗：终端显示识别输出，同时保存最近5份日志。

set -e

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
PROJECT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd -P)"
START_SCRIPT="$SCRIPT_DIR/start_vision.sh"
RUNTIME_DIR="$PROJECT_DIR/runtime"
LOG_DIR="$PROJECT_DIR/logs"
DAEMON_PID_FILE="$RUNTIME_DIR/vision_daemon.pid"
CHILD_PID_FILE="$RUNTIME_DIR/vision_child.pid"
BOOT_ID_FILE="$RUNTIME_DIR/vision_daemon.boot_id"

MAX_RESTART=50
RESTART_COOLDOWN=5
STOP_REQUESTED=false
VISION_PID=""

pid_is_running() {
    local pid="${1:-}"
    local expected="${2:-}"
    [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null || return 1
    if [ -n "$expected" ]; then
        [ -r "/proc/$pid/cmdline" ] || return 1
        tr '\0' ' ' < "/proc/$pid/cmdline" | grep -Eq "$expected"
    fi
}

read_pid_file() {
    local file="$1"
    if [ -f "$file" ]; then
        tr -dc '0-9' < "$file"
    fi
}

stop_pid() {
    local pid="$1"
    local expected="${2:-}"
    if ! pid_is_running "$pid" "$expected"; then
        return 0
    fi
    kill "$pid" 2>/dev/null || true
    for _ in 1 2 3 4 5; do
        pid_is_running "$pid" "$expected" || return 0
        sleep 1
    done
    kill -9 "$pid" 2>/dev/null || true
}

do_stop() {
    local child_pid
    local daemon_pid
    child_pid="$(read_pid_file "$CHILD_PID_FILE")"
    daemon_pid="$(read_pid_file "$DAEMON_PID_FILE")"
    stop_pid "$child_pid" 'logistics_vision|start_vision.sh'
    if [ "$daemon_pid" != "$$" ]; then
        stop_pid "$daemon_pid" 'vision_daemon.sh'
    fi
    rm -f "$CHILD_PID_FILE" "$DAEMON_PID_FILE"
    echo "[Daemon] 已停止视觉程序和看门狗。"
}

do_status() {
    local daemon_pid
    local child_pid
    daemon_pid="$(read_pid_file "$DAEMON_PID_FILE")"
    child_pid="$(read_pid_file "$CHILD_PID_FILE")"
    if pid_is_running "$daemon_pid" 'vision_daemon.sh'; then
        echo "看门狗：运行中，PID=$daemon_pid"
    else
        echo "看门狗：未运行"
    fi
    if pid_is_running "$child_pid" 'logistics_vision|start_vision.sh'; then
        echo "视觉程序：运行中，PID=$child_pid"
    else
        echo "视觉程序：未运行"
    fi
}

handle_stop_signal() {
    STOP_REQUESTED=true
    if pid_is_running "$VISION_PID" 'logistics_vision|start_vision.sh'; then
        kill "$VISION_PID" 2>/dev/null || true
    fi
}

cleanup() {
    if pid_is_running "$VISION_PID" 'logistics_vision|start_vision.sh'; then
        stop_pid "$VISION_PID" 'logistics_vision|start_vision.sh'
    fi
    rm -f "$CHILD_PID_FILE" "$DAEMON_PID_FILE"
}

rotate_logs() {
    local logs=()
    local index
    shopt -s nullglob
    logs=("$LOG_DIR"/vision_*.log)
    shopt -u nullglob
    if [ "${#logs[@]}" -le 5 ]; then
        return 0
    fi
    mapfile -t logs < <(printf '%s\n' "${logs[@]}" | sort -r)
    for ((index=5; index<${#logs[@]}; ++index)); do
        rm -f -- "${logs[$index]}"
    done
}

do_run() {
    mkdir -p "$RUNTIME_DIR" "$LOG_DIR"

    local existing_daemon
    existing_daemon="$(read_pid_file "$DAEMON_PID_FILE")"
    if pid_is_running "$existing_daemon" 'vision_daemon.sh'; then
        echo "[Daemon] 已经在运行，PID=$existing_daemon"
        exit 0
    fi

    printf '%s\n' "$$" > "$DAEMON_PID_FILE"
    trap handle_stop_signal SIGINT SIGTERM SIGHUP
    trap cleanup EXIT

    rotate_logs
    local log_file="$LOG_DIR/vision_$(date +%Y%m%d_%H%M%S).log"
    local boot_id
    local saved_boot_id=""
    local first_start_this_boot=true
    boot_id="$(cat /proc/sys/kernel/random/boot_id)"
    if [ -f "$BOOT_ID_FILE" ]; then
        saved_boot_id="$(cat "$BOOT_ID_FILE")"
    fi
    if [ "$saved_boot_id" = "$boot_id" ]; then
        first_start_this_boot=false
    else
        printf '%s\n' "$boot_id" > "$BOOT_ID_FILE"
    fi

    echo "============================================"
    echo " NewMaker Competition Vision Daemon"
    echo " 工程：$PROJECT_DIR"
    echo " 日志：$log_file"
    echo "============================================"

    local restart_count=0
    while [ "$STOP_REQUESTED" = false ]; do
        local run_args=()
        # 新开机首次运行按vision.yaml清理上场进度；同次开机内重启则保留本场进度。
        if [ "$first_start_this_boot" = false ] || [ "$restart_count" -gt 0 ]; then
            run_args+=("--keep-progress-on-start")
        fi

        "$START_SCRIPT" "${run_args[@]}" > >(tee -a "$log_file") 2>&1 &
        VISION_PID=$!
        printf '%s\n' "$VISION_PID" > "$CHILD_PID_FILE"
        echo "[Daemon] 视觉PID=$VISION_PID"

        set +e
        wait "$VISION_PID"
        local exit_code=$?
        set -e
        VISION_PID=""
        rm -f "$CHILD_PID_FILE"

        if [ "$STOP_REQUESTED" = true ]; then
            break
        fi

        restart_count=$((restart_count + 1))
        if [ "$restart_count" -gt "$MAX_RESTART" ]; then
            echo "[Daemon] 超过最大重启次数，停止看门狗。"
            exit 1
        fi
        echo "[Daemon] 程序退出码=$exit_code，${RESTART_COOLDOWN}秒后第${restart_count}次重启。"
        sleep "$RESTART_COOLDOWN"
    done
}

case "${1:-run}" in
    run)    do_run ;;
    stop)   do_stop ;;
    status) do_status ;;
    *)
        echo "用法：$0 {run|stop|status}"
        exit 1
        ;;
esac
