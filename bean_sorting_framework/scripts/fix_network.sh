#!/bin/bash
# ============================================================
#  虚拟机网络修复脚本
#  解决: Ubuntu开机连不上主机网络, 每次需"还原网络设置"
#
#  用法:
#    ./fix_network.sh        手动跑一次修复
#    ./fix_network.sh on     开机自启 (root)
#    ./fix_network.sh off    关闭自启
# ============================================================

SERVICE_NAME="vm-network-fix"
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"
SELF_PATH="$(realpath "$0")"

# ============================================================
#  fix: 核心修复逻辑
# ============================================================
do_fix() {
    echo "[fix] 开始修复虚拟机网络..."

    # 1. 找到主网卡
    IFACE=$(ip route | grep default | awk '{print $5}' | head -1)
    if [ -z "$IFACE" ]; then
        # 没有默认路由, 尝试第一个非lo网卡
        IFACE=$(ip -o link show | grep -v lo | awk -F': ' '{print $2}' | head -1)
    fi
    echo "[fix] 网卡: ${IFACE}"

    # 2. 重置网卡
    sudo ip link set "${IFACE}" down 2>/dev/null
    sleep 2
    sudo ip link set "${IFACE}" up 2>/dev/null

    # 3. 重启 NetworkManager
    sudo systemctl restart NetworkManager 2>/dev/null || true

    # 4. 等 DHCP 分配 IP
    for i in $(seq 1 15); do
        sleep 1
        if ip route | grep -q default; then
            echo "[fix] 网络已恢复 $(date '+%H:%M:%S')"
            return 0
        fi
        echo "  等待 DHCP... (${i}/15)"
    done

    echo "[fix] 超时, 尝试 dhclient"
    sudo dhclient -v "${IFACE}" 2>/dev/null || true
    sleep 3

    if ip route | grep -q default; then
        echo "[fix] dhclient 后网络恢复"
    else
        echo "[fix] 修复失败, 可能需要手动操作"
    fi
}

# ============================================================
#  on: 创建 systemd 服务 (开机自动跑)
# ============================================================
do_on() {
    chmod +x "${SELF_PATH}"
    sudo tee "${SERVICE_FILE}" > /dev/null << EOF
[Unit]
Description=VM Network Fix
After=network.target NetworkManager.service
Wants=network.target

[Service]
Type=oneshot
ExecStart=${SELF_PATH} fix
RemainAfterExit=yes
TimeoutStartSec=60

[Install]
WantedBy=multi-user.target
EOF
    sudo systemctl daemon-reload
    sudo systemctl enable "${SERVICE_NAME}"
    echo "[OK] 开机网络修复已启用"
    echo "  关闭: $0 off"
}

# ============================================================
#  off: 关闭自启
# ============================================================
do_off() {
    sudo systemctl disable "${SERVICE_NAME}" 2>/dev/null
    sudo rm -f "${SERVICE_FILE}"
    sudo systemctl daemon-reload
    echo "[OK] 已关闭"
}

# ============================================================
case "${1}" in
    fix)  do_fix ;;
    on)   do_on ;;
    off)  do_off ;;
    *)
        echo "用法: $0 {fix|on|off}"
        echo "  fix   手动修复一次"
        echo "  on    配置开机自动修复"
        echo "  off   关闭开机自动修复"
        ;;
esac
