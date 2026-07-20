/**
 * @file VirtualSerial.cpp
 * @brief 串口通信类实现（单向通信：TX 依次发送数字包 + 豆子包）
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
VirtualSerial::VirtualSerial(const std::string& portName)
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
    DIR* dir = opendir("/dev");
    if (!dir) return "";

    struct dirent* entry;
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
// sendFrame — 通用发送（写入 + 重试 + 重连）
// ============================================================
bool VirtualSerial::sendFrame(const std::vector<uint8_t>& frame, int maxRetries)
{
    // ---- 状态检查 ----
    if (!IsOpen()) {
        std::cerr << "[VirtualSerial] Error: Serial port not open" << std::endl;
        return false;
    }

    // ---- 发送 + 重试 ----
    for (int retry = 0; retry < maxRetries; retry++) {
        ssize_t written = write(serialFd_, frame.data(), frame.size());
        if (written == static_cast<ssize_t>(frame.size()))
            return true;
        if (retry < maxRetries - 1)
            usleep(1000);
    }

    std::cerr << "[VirtualSerial] Error: Failed to send frame after "
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
// 打印 hex 帧（调试用）
// ============================================================
static void printHexFrame(const std::string& label, const std::vector<int>& ids,
                          const std::vector<uint8_t>& frame)
{
    std::ios oldState(nullptr);
    oldState.copyfmt(std::cout);

    std::cout << "[VirtualSerial] TX " << label << ": ";
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i > 0) std::cout << " ";
        std::cout << ids[i];
    }
    std::cout << "  frame=";
    for (size_t i = 0; i < frame.size(); ++i) {
        std::cout << std::uppercase << std::hex << std::setw(2)
                  << std::setfill('0') << static_cast<int>(frame[i])
                  << (i < frame.size() - 1 ? " " : "");
    }
    std::cout.copyfmt(oldState);
    std::cout << std::endl;
}

// ============================================================
// sendDigitPacket — 发送数字包（固定 8 字节）
// ============================================================
bool VirtualSerial::sendDigitPacket(const std::vector<int>& digitIds, int maxRetries)
{
    if (digitIds.size() != 5) {
        std::cerr << "[VirtualSerial] Error: Digit packet requires exactly 5 IDs, got "
                  << digitIds.size() << std::endl;
        return false;
    }

    // ---- 模拟模式 ----
    if (simulated_) {
        if (txLogEnabled_) {
            std::cout << "[VirtualSerial] Simulated TX DIGIT: ";
            for (size_t i = 0; i < digitIds.size(); ++i) {
                if (i > 0) std::cout << " ";
                std::cout << digitIds[i];
            }
            std::cout << std::endl;
        }
        return true;
    }

    // ---- 填充 DigitPacket ----
    DigitPacket packet;
    for (size_t i = 0; i < 5; ++i)
        packet.digits[i] = static_cast<uint8_t>(digitIds[i]);

    std::vector<uint8_t> frame = toVector(packet);

    // ---- 发送日志 ----
    if (txLogEnabled_)
        printHexFrame("DIGIT", digitIds, frame);

    return sendFrame(frame, maxRetries);
}

// ============================================================
// sendBeanPacket — 发送豆子包（固定 6 字节）
// ============================================================
bool VirtualSerial::sendBeanPacket(const std::vector<int>& beanIds, int maxRetries)
{
    if (beanIds.size() != 3) {
        std::cerr << "[VirtualSerial] Error: Bean packet requires exactly 3 IDs, got "
                  << beanIds.size() << std::endl;
        return false;
    }

    // ---- 模拟模式 ----
    if (simulated_) {
        if (txLogEnabled_) {
            std::cout << "[VirtualSerial] Simulated TX BEAN: ";
            for (size_t i = 0; i < beanIds.size(); ++i) {
                if (i > 0) std::cout << " ";
                std::cout << beanIds[i];
            }
            std::cout << std::endl;
        }
        return true;
    }

    // ---- 填充 BeanPacket ----
    BeanPacket packet;
    for (size_t i = 0; i < 3; ++i)
        packet.beans[i] = static_cast<uint8_t>(beanIds[i]);

    std::vector<uint8_t> frame = toVector(packet);

    // ---- 发送日志 ----
    if (txLogEnabled_)
        printHexFrame("BEAN", beanIds, frame);

    return sendFrame(frame, maxRetries);
}
