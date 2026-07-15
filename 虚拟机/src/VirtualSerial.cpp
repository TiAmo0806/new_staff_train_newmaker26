/**
 * @file VirtualSerial.cpp
 * @brief 串口通信类实现（双向通信：TX 发送检测结果 + RX 接收电控指令）
 * @author lxy
 * @date 2025-10-24
 */

#include "VirtualSerial.h"
#include "packet.hpp"

#include <iostream>
#include <iomanip>
#include <cstring>

#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>

// ============================================================
// 构造 / 析构
// ============================================================
VirtualSerial::VirtualSerial(const std::string &portName)
    : serialFd_(-1), portName_(portName)
{
}

VirtualSerial::~VirtualSerial()
{
    Close();
}

// ============================================================
// Open / Close
// ============================================================
bool VirtualSerial::Open()
{
    if (IsOpen()) return true;

    serialFd_ = open(portName_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (serialFd_ < 0) {
        std::cerr << "[VirtualSerial] Error: Failed to open serial port" << std::endl;
        return false;
    }

    if (!ConfigurePort()) {
        Close();
        return false;
    }

    return true;
}

void VirtualSerial::Close()
{
    if (IsOpen()) {
        close(serialFd_);
        serialFd_ = -1;
    }
    rxBuffer_.clear();
}

// ============================================================
// sendDetectionOrder — 发送检测排序结果
// ============================================================
bool VirtualSerial::sendDetectionOrder(const std::vector<int>& classIds,
                                       int maxRetries)
{
    // ---- 模拟模式 ----
    if (simulated_) {
        if (txLogEnabled_) {
            std::cout << "[VirtualSerial] Simulated TX order: ";
            for (size_t i = 0; i < classIds.size(); ++i) {
                if (i > 0) std::cout << " ";
                std::cout << classIds[i];
            }
            std::cout << std::endl;
        }
        return true;
    }

    // ---- 状态检查 ----
    if (!IsOpen()) {
        std::cerr << "[VirtualSerial] Error: Serial port not open" << std::endl;
        return false;
    }

    size_t n = classIds.size();
    if (n > 4) {   // SendPacket::class_ids[4] 最多 4 个
        std::cerr << "[VirtualSerial] Error: Too many objects (" << n
                  << "), max 4" << std::endl;
        return false;
    }

    // ---- 填充 SendPacket（仿 packet1.hpp 的 packed struct 模式） ----
    SendPacket packet;
    packet.count = static_cast<uint8_t>(n);
    for (size_t i = 0; i < n; ++i)
        packet.class_ids[i] = static_cast<uint8_t>(classIds[i]);

    // toVector 内部自动计算 CRC16 并拼接到帧尾
    std::vector<uint8_t> frame = toVector(packet);

    // ---- 发送日志 ----
    if (txLogEnabled_) {
        std::cout << "[VirtualSerial] TX order: ";
        for (size_t i = 0; i < n; ++i) {
            if (i > 0) std::cout << " ";
            std::cout << classIds[i];
        }
        std::cout << "  frame=";
        std::ios oldState(nullptr);
        oldState.copyfmt(std::cout);
        for (size_t i = 0; i < frame.size(); ++i)
            std::cout << std::uppercase << std::hex << std::setw(2)
                      << std::setfill('0') << static_cast<int>(frame[i])
                      << (i < frame.size() - 1 ? " " : "");
        std::cout.copyfmt(oldState);
        std::cout << std::endl;
    }

    // ---- 发送 + 重试 ----
    for (int retry = 0; retry < maxRetries; retry++) {
        ssize_t written = write(serialFd_, frame.data(), frame.size());
        if (written == static_cast<ssize_t>(frame.size()))
            return true;
        if (retry < maxRetries - 1)
            usleep(1000);
    }

    std::cerr << "[VirtualSerial] Error: Failed to send order after "
              << maxRetries << " retries" << std::endl;

    // ---- 重连重试 ----
    if (autoReconnect_ && TryReconnect()) {
        std::cout << "[VirtualSerial] Retrying send after reconnect..." << std::endl;
        if (write(serialFd_, frame.data(), frame.size()) ==
            static_cast<ssize_t>(frame.size()))
            return true;
    }

    return false;
}

// ============================================================
// ConfigurePort — 配置串口参数（115200 8N1 原始模式）
// ============================================================
bool VirtualSerial::ConfigurePort()
{
    struct termios tty;
    std::memset(&tty, 0, sizeof(tty));

    if (tcgetattr(serialFd_, &tty) != 0) {
        std::cerr << "[VirtualSerial] Error: Failed to get port attributes" << std::endl;
        return false;
    }

    // 波特率
    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);

    // 控制模式：8 数据位、无校验、1 停止位、无硬件流控
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CREAD | CLOCAL;

    // 本地模式：原始模式
    tty.c_lflag &= ~ICANON;
    tty.c_lflag &= ~ECHO;
    tty.c_lflag &= ~ECHOE;
    tty.c_lflag &= ~ECHONL;
    tty.c_lflag &= ~ISIG;

    // 输入模式：不做任何转换
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    // 输出模式：原始输出
    tty.c_oflag &= ~OPOST;
    tty.c_oflag &= ~ONLCR;

    if (tcsetattr(serialFd_, TCSANOW, &tty) != 0) {
        std::cerr << "[VirtualSerial] Error: Failed to set port attributes" << std::endl;
        return false;
    }

    tcflush(serialFd_, TCIOFLUSH);
    return true;
}

// ============================================================
// FindAvailablePort — 扫描 /dev 下的串口设备
// ============================================================
std::string VirtualSerial::FindAvailablePort()
{
    DIR *dir = opendir("/dev");
    if (!dir) return "";

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.find("ttyACM") == 0 || name.find("ttyUSB") == 0) {
            std::string fullPath = "/dev/" + name;
            int testFd = open(fullPath.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
            if (testFd >= 0) {
                close(testFd);
                closedir(dir);
                return fullPath;
            }
        }
    }
    closedir(dir);
    return "";
}

// ============================================================
// TryReconnect — 自动重连
// ============================================================
bool VirtualSerial::TryReconnect()
{
    if (!autoReconnect_) return false;

    Close();

    constexpr int kMaxWaitMs = 5000;
    constexpr int kIntervalMs = 200;
    std::string newPort;
    for (int waited = 0; waited < kMaxWaitMs; waited += kIntervalMs) {
        newPort = FindAvailablePort();
        if (!newPort.empty()) break;
        usleep(kIntervalMs * 1000);
    }

    if (newPort.empty()) {
        std::cerr << "[VirtualSerial] Reconnect: No available port found" << std::endl;
        return false;
    }

    if (newPort != portName_) {
        std::cout << "[VirtualSerial] Reconnect: Port changed from "
                  << portName_ << " to " << newPort << std::endl;
        portName_ = newPort;
    }

    if (Open()) {
        std::cout << "[VirtualSerial] Reconnect: Successfully reconnected to "
                  << portName_ << std::endl;
        return true;
    }

    return false;
}

// ============================================================
// PollReceive — 非阻塞轮询接收（双向通信 RX 路径）
// ============================================================
void VirtualSerial::PollReceive()
{
    if (simulated_ || !IsOpen()) return;

    // ---- 非阻塞读取串口 ----
    uint8_t temp[64];
    ssize_t n = read(serialFd_, temp, sizeof(temp));
    if (n > 0) {
        rxBuffer_.insert(rxBuffer_.end(), temp, temp + n);

        // 防止垃圾数据导致缓冲区无限增长（MCU 帧为 4 字节，128 足够）
        if (rxBuffer_.size() > 128) {
            rxBuffer_.erase(rxBuffer_.begin(),
                            rxBuffer_.begin() + (rxBuffer_.size() - 128));
        }
    }

    // ---- 解析缓冲区中的帧（可能有多帧积压） ----
    while (ParseRxBuffer()) {
        // ParseRxBuffer 内部处理成功时移除已处理字节
    }
}

// ============================================================
// ParseRxBuffer — 从缓冲区扫描并提取一个完整帧
// ============================================================
bool VirtualSerial::ParseRxBuffer()
{
    // 查找帧头 0x5A
    auto it = std::find(rxBuffer_.begin(), rxBuffer_.end(), 0x5A);
    if (it == rxBuffer_.end()) {
        // 没有帧头，清空（全是垃圾数据）
        rxBuffer_.clear();
        return false;
    }

    // 丢弃帧头之前的垃圾字节
    if (it != rxBuffer_.begin()) {
        rxBuffer_.erase(rxBuffer_.begin(), it);
    }

    // 帧头在位置 0，检查是否有完整帧（固定 4 字节）
    if (rxBuffer_.size() < 4) return false;   // 等待更多数据

    // 提取完整帧
    uint8_t frame[4];
    std::copy(rxBuffer_.begin(), rxBuffer_.begin() + 4, frame);

    // 校验 CRC 并分发
    HandleValidFrame(frame);

    // 无论校验是否通过，移除这 4 字节（无效帧也要丢弃，防止死循环）
    rxBuffer_.erase(rxBuffer_.begin(), rxBuffer_.begin() + 4);
    return true;
}

// ============================================================
// HandleValidFrame — CRC 校验 + 回调分发
// ============================================================
void VirtualSerial::HandleValidFrame(const uint8_t * frame)
{
    ReceivePacket packet;
    if (!parseReceivePacket(frame, packet)) {
        // CRC 校验失败或 header 不匹配，静默丢弃
        return;
    }

    if (rxCallback_) {
        rxCallback_(packet.action);
    }
}
