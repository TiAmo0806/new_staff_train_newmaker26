/**
 * @file SerialPort.cpp
 * @brief 串口通信实现 — 与 STM32F407 电控板通过 USB CDC 通信
 *
 * 发送端：封装 12 字节 VisionToMCUPacket（自动计算 CRC-16/CCITT）
 * 接收端：以 0x5A 包头同步接收 4 字节 MCUToVisionPacket（自动验证 CRC）
 *
 * @see include/SerialPort.hpp
 * @see docs/communication_protocol.md
 */

#include "../include/SerialPort.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <iostream>
#include <chrono>
#include <thread>

using namespace rm_serial_driver;

SerialPort::SerialPort(const std::string& port, int baud_rate) {
    open(port, baud_rate);
}

SerialPort::~SerialPort() {
    close();
}

// ============================================================
//  open —— 打开串口并配置参数
//
//  配置说明:
//    - 波特率: 115200（由构造参数指定）
//    - 数据位: 8 位（CS8）
//    - 停止位: 1 位（CSTOPB 未设置）
//    - 校验位: 无（PARENB 未设置）
//    - 非规范模式（ICANON 未设置）— 原始二进制传输
//    - 无硬件流控（IXON/OFF/OFF 未设置）
// ============================================================

bool SerialPort::open(const std::string& port, int baud_rate) {
    fd = ::open(port.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd == -1) {
        std::cerr << "[Serial] Failed to open port: " << port << std::endl;
        return false;
    }

    struct termios options;
    tcgetattr(fd, &options);

    // 设置波特率
    speed_t speed;
    switch (baud_rate) {
        case 9600:   speed = B9600;   break;
        case 19200:  speed = B19200;  break;
        case 38400:  speed = B38400;  break;
        case 57600:  speed = B57600;  break;
        case 115200: speed = B115200; break;
        default:     speed = B115200;
    }
    cfsetispeed(&options, speed);
    cfsetospeed(&options, speed);

    // 控制模式标志：本地连接、接收使能
    options.c_cflag |= (CLOCAL | CREAD);
    // 无校验
    options.c_cflag &= ~PARENB;
    // 1 位停止位
    options.c_cflag &= ~CSTOPB;
    // 8 位数据位
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;

    // 本地模式：非规范模式（关闭规范输入、回显等）
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

    // 输入模式：关闭软件流控
    options.c_iflag &= ~(IXON | IXOFF | IXANY);

    // 输出模式：原始输出（不进行转换）
    options.c_oflag &= ~OPOST;

    tcsetattr(fd, TCSANOW, &options);
    std::cout << "[Serial] Opened: " << port << " @ " << baud_rate << std::endl;
    return true;
}

void SerialPort::close() {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
        std::cout << "[Serial] Closed" << std::endl;
    }
}

// ============================================================
//  calcCRC16 —— CRC-16/CCITT 校验计算
//
//  与电控（STM32）侧使用的算法完全一致，确保数据完整性。
//
//  算法参数:
//    多项式:  0x1021 (CCITT 标准)
//    初始值:  0xFFFF
//    不反转输入/输出
//
//  计算范围: 从 header 开始到 checksum 之前的所有字节
// ============================================================

uint16_t SerialPort::calcCRC16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
        crc &= 0xFFFF;  // 确保只保留低 16 位
    }
    return crc;
}

// ============================================================
//  发送（视觉 → 电控）
//
//  流程:
//    1. 拷贝一份数据包
//    2. 对前 10 字节（header + flags + pitch + yaw）计算 CRC
//    3. 填入 checksum 字段
//    4. 序列化为 12 字节并写入串口
// ============================================================

bool SerialPort::sendPacket(const VisionToMCUPacket& packet_in) {
    if (fd < 0) return false;

    // 拷贝一份，避免修改原始数据
    VisionToMCUPacket packet = packet_in;
    // CRC 计算范围：整个包除 checksum 外的所有字节
    size_t crc_len = sizeof(VisionToMCUPacket) - sizeof(packet.checksum);
    packet.checksum = calcCRC16(reinterpret_cast<const uint8_t*>(&packet), crc_len);

    // 序列化并写入串口
    std::vector<uint8_t> data = toVector(packet);
    ssize_t sent = write(fd, data.data(), data.size());
    if (sent < 0) return false;

    std::cout << "[Serial] Send: type=" << (int)packet.target_type
              << " id=" << (int)packet.target_id
              << " tracking=" << (int)packet.tracking << std::endl;
    return true;
}

bool SerialPort::send(const std::string& data) {
    if (fd < 0) return false;
    ssize_t sent = write(fd, data.c_str(), data.length());
    return sent >= 0;
}

// ============================================================
//  ⭐ 接收（电控 → 视觉）
//
//  在指定超时时间内，不断从串口读取字节，以 0x5A 包头同步对齐，
//  收到完整 4 字节后验证 CRC 和包头，通过则返回 true。
//
//  同步策略:
//    视觉可能在任何时刻开始读取，串口缓冲区中可能有旧数据或不完整帧。
//    本函数通过搜索 0x5A 包头来重新同步：
//      - 如果当前字节不是 0x5A 且 buffer 为空 → 丢弃
//      - 如果 buffer 已满 4 字节但校验失败 → 丢弃第一个字节，继续搜索
// ============================================================

bool SerialPort::receivePacket(MCUToVisionPacket& packet, size_t timeout_ms) {
    if (fd < 0) return false;

    auto start = std::chrono::steady_clock::now();
    constexpr size_t PACKET_SIZE = sizeof(MCUToVisionPacket);   // 4 字节
    std::vector<uint8_t> buffer;
    buffer.reserve(PACKET_SIZE);

    while (true) {
        // 超时检查
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count() > timeout_ms) {
            return false;
        }

        uint8_t byte;
        int n = read(fd, &byte, 1);
        if (n > 0) {
            // 搜索 0x5A 包头：如果 buffer 为空且收到的不是包头，则丢弃
            if (buffer.empty() && byte != 0x5A) continue;
            buffer.push_back(byte);

            if (buffer.size() >= PACKET_SIZE) {
                // 已收到足够字节 → 尝试解包
                // 取前 PACKET_SIZE 字节（防止 buffer 积累导致的溢出）
                std::copy(buffer.begin(), buffer.begin() + PACKET_SIZE,
                          reinterpret_cast<uint8_t*>(&packet));

                // 验证 CRC（对前 2 字节计算）
                uint16_t recv_crc  = packet.checksum;
                uint16_t calc_crc  = calcCRC16(buffer.data(), PACKET_SIZE - 2);

                if (recv_crc == calc_crc && packet.header == 0x5A) {
                    // ✅ 校验成功
                    std::cout << "[Serial] Recv feedback: type=" << (int)packet.feedback_type << std::endl;
                    return true;
                } else {
                    // ❌ 校验失败：可能是帧同步丢失，丢弃第一个字节继续搜索
                    buffer.erase(buffer.begin());
                }
            }
        }
        // 避免忙等，每次 read 间隔 10ms
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// ============================================================
//  receive —— 接收原始字符串数据（备用接口）
// ============================================================

std::string SerialPort::receive(size_t timeout_ms) {
    if (fd < 0) return "";

    char buffer[256];
    std::string result;
    auto start = std::chrono::steady_clock::now();

    while (true) {
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count() > timeout_ms) {
            break;
        }
        int n = read(fd, buffer, sizeof(buffer) - 1);
        if (n > 0) {
            buffer[n] = '\0';
            result += buffer;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return result;
}
