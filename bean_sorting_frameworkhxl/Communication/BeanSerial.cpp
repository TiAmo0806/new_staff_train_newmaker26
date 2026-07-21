// Communication/BeanSerial.cpp
// 对齐 mvs_openvino_demo VirtualSerial — 单向 TX: 数字包(8B) + 豆子包(6B)

#include "BeanSerial.h"
#include "BeanProtocol.h"
#include "CRC16.hpp"

#include <iostream>
#include <iomanip>
#include <cstring>

#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>

BeanSerial::BeanSerial(const std::string& portName)
    : portName_(portName) {}

BeanSerial::~BeanSerial() { Close(); }

// ================================================================
// 串口打开 / 关闭
// ================================================================
bool BeanSerial::Open() {
    if (IsOpen()) return true;
    if (portName_.find('*') != std::string::npos) {
        std::string resolved = FindAvailablePort();
        if (resolved.empty()) {
            std::cerr << "[BeanSerial] Error: 找不到匹配 " << portName_ << " 的设备" << std::endl;
            return false;
        }
        std::cout << "[BeanSerial] 解析 " << portName_ << " → " << resolved << std::endl;
        portName_ = resolved;
    }

    fd_ = open(portName_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
        std::cerr << "[BeanSerial] Error: 无法打开串口 " << portName_ << std::endl;
        return false;
    }

    if (!ConfigurePort()) { Close(); return false; }

    std::cout << "[BeanSerial] 已连接 " << portName_ << std::endl;
    return true;
}

void BeanSerial::Close() {
    if (fd_ >= 0) { close(fd_); fd_ = -1; }
}

// ================================================================
// 串口配置 115200 8N1
// ================================================================
bool BeanSerial::ConfigurePort() {
    struct termios tty;
    std::memset(&tty, 0, sizeof(tty));

    if (tcgetattr(fd_, &tty) != 0) {
        std::cerr << "[BeanSerial] Error: tcgetattr 失败" << std::endl;
        return false;
    }

    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);

    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CREAD | CLOCAL;

    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHONL | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK | BRKINT | PARMRK |
                     ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_oflag &= ~OPOST;
    tty.c_oflag &= ~ONLCR;

    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
        std::cerr << "[BeanSerial] Error: tcsetattr 失败" << std::endl;
        return false;
    }

    tcflush(fd_, TCIOFLUSH);
    return true;
}

// ================================================================
// 通用发送
// ================================================================
bool BeanSerial::sendFrame(const std::vector<uint8_t>& frame, int maxRetries) {
    if (!IsOpen()) {
        std::cerr << "[BeanSerial] Error: 串口未打开" << std::endl;
        return false;
    }

    if (txLogEnabled_) {
        std::cout << "[BeanSerial] TX frame: ";
        std::ios oldState(nullptr);
        oldState.copyfmt(std::cout);
        for (size_t i = 0; i < frame.size(); ++i)
            std::cout << std::uppercase << std::hex << std::setw(2)
                      << std::setfill('0') << static_cast<int>(frame[i])
                      << (i < frame.size() - 1 ? " " : "");
        std::cout.copyfmt(oldState);
        std::cout << std::endl;
    }

    for (int retry = 0; retry < maxRetries; ++retry) {
        if (write(fd_, frame.data(), frame.size()) == static_cast<ssize_t>(frame.size()))
            return true;
        if (retry < maxRetries - 1)
            usleep(1000);
    }

    std::cerr << "[BeanSerial] Error: 发送失败 (" << maxRetries << " retries)" << std::endl;

    if (autoReconnect_ && TryReconnect()) {
        std::cout << "[BeanSerial] 重连后重试发送..." << std::endl;
        if (write(fd_, frame.data(), frame.size()) == static_cast<ssize_t>(frame.size()))
            return true;
    }

    return false;
}

// ================================================================
// 发送数字包 (固定 8 字节)
// ================================================================
bool BeanSerial::sendDigitPacket(const std::vector<int>& digitIds, int maxRetries) {
    if (digitIds.size() != 5) {
        std::cerr << "[BeanSerial] Error: 数字包需要5个ID, 实际 " << digitIds.size() << std::endl;
        return false;
    }

    if (simulated_) {
        if (txLogEnabled_) {
            std::cout << "[BeanSerial] Simulated DIGIT: ";
            for (size_t i = 0; i < digitIds.size(); ++i)
                std::cout << (i ? " " : "") << digitIds[i];
            std::cout << std::endl;
        }
        return true;
    }

    bean_sorting::DigitPacket packet;
    for (size_t i = 0; i < 5; ++i)
        packet.digits[i] = static_cast<uint8_t>(digitIds[i]);

    auto frame = bean_sorting::toVector(packet);

    if (txLogEnabled_) {
        std::cout << "[BeanSerial] DIGIT: ";
        for (size_t i = 0; i < digitIds.size(); ++i)
            std::cout << (i ? " " : "") << digitIds[i];
        std::cout << std::endl;
    }

    return sendFrame(frame, maxRetries);
}

// ================================================================
// 发送豆子包 (固定 6 字节)
// ================================================================
bool BeanSerial::sendBeanPacket(const std::vector<int>& beanIds, int maxRetries) {
    if (beanIds.size() != 3) {
        std::cerr << "[BeanSerial] Error: 豆子包需要3个ID, 实际 " << beanIds.size() << std::endl;
        return false;
    }

    if (simulated_) {
        if (txLogEnabled_) {
            std::cout << "[BeanSerial] Simulated BEAN: ";
            for (size_t i = 0; i < beanIds.size(); ++i)
                std::cout << (i ? " " : "") << beanIds[i];
            std::cout << std::endl;
        }
        return true;
    }

    bean_sorting::BeanPacket packet;
    for (size_t i = 0; i < 3; ++i)
        packet.beans[i] = static_cast<uint8_t>(beanIds[i]);

    auto frame = bean_sorting::toVector(packet);

    if (txLogEnabled_) {
        std::cout << "[BeanSerial] BEAN: ";
        for (size_t i = 0; i < beanIds.size(); ++i)
            std::cout << (i ? " " : "") << beanIds[i];
        std::cout << std::endl;
    }

    return sendFrame(frame, maxRetries);
}

// ================================================================
// 自动重连
// ================================================================
std::string BeanSerial::FindAvailablePort() {
    DIR* dir = opendir("/dev");
    if (!dir) return "";

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.find("ttyACM") == 0 || name.find("ttyUSB") == 0) {
            std::string full = "/dev/" + name;
            int testFd = open(full.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
            if (testFd >= 0) {
                close(testFd);
                closedir(dir);
                return full;
            }
        }
    }
    closedir(dir);
    return "";
}

bool BeanSerial::TryReconnect() {
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
        std::cerr << "[BeanSerial] 重连: 未找到可用设备" << std::endl;
        return false;
    }

    if (newPort != portName_) {
        std::cout << "[BeanSerial] 重连: 设备变更 " << portName_
                  << " → " << newPort << std::endl;
        portName_ = newPort;
    }

    if (Open()) {
        std::cout << "[BeanSerial] 重连成功: " << portName_ << std::endl;
        return true;
    }
    return false;
}
