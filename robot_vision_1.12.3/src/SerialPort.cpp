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

bool SerialPort::open(const std::string& port, int baud_rate) {
    fd = ::open(port.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd == -1) {
        std::cerr << "[Serial] Failed to open port: " << port << std::endl;
        return false;
    }
    
    struct termios options;
    tcgetattr(fd, &options);
    
    speed_t speed;
    switch (baud_rate) {
        case 9600: speed = B9600; break;
        case 19200: speed = B19200; break;
        case 38400: speed = B38400; break;
        case 57600: speed = B57600; break;
        case 115200: speed = B115200; break;
        default: speed = B115200;
    }
    cfsetispeed(&options, speed);
    cfsetospeed(&options, speed);
    
    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
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

uint16_t SerialPort::calcCRC16(const uint8_t* data, size_t len) {
    // ⭐ MCU 使用 CRC-16/CCITT (多项式 0x1021, 初始值 0xFFFF, 不反转)
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
        crc &= 0xFFFF;
    }
    return crc;
}

// ============================================
// 发送（视觉→电控）
// ============================================

bool SerialPort::sendPacket(const VisionToMCUPacket& packet_in) {
    if (fd < 0) return false;

    // ⭐ 拷贝一份，计算并填充 CRC
    VisionToMCUPacket packet = packet_in;
    size_t crc_len = sizeof(VisionToMCUPacket) - sizeof(packet.checksum);
    packet.checksum = calcCRC16(reinterpret_cast<const uint8_t*>(&packet), crc_len);

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

// ============================================
// ⭐ 接收（电控→视觉）
// ============================================

bool SerialPort::receivePacket(MCUToVisionPacket& packet, size_t timeout_ms) {
    if (fd < 0) return false;

    auto start = std::chrono::steady_clock::now();
    std::vector<uint8_t> buffer;
    buffer.reserve(sizeof(MCUToVisionPacket));

    while (true) {
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count() > timeout_ms) {
            return false;
        }

        uint8_t byte;
        int n = read(fd, &byte, 1);
        if (n > 0) {
            if (buffer.empty() && byte != 0x5A) continue;
            buffer.push_back(byte);

            if (buffer.size() >= sizeof(MCUToVisionPacket)) {
                packet = fromMCUVector(buffer);

                // 验证CRC
                uint16_t recv_crc = packet.checksum;
                packet.checksum = 0;
                uint16_t calc_crc = calcCRC16(buffer.data(), sizeof(MCUToVisionPacket) - 2);
                packet.checksum = recv_crc;

                if (recv_crc == calc_crc && packet.header == 0x5A) {
                    std::cout << "[Serial] Recv feedback: type=" << (int)packet.feedback_type << std::endl;
                    return true;
                } else {
                    buffer.clear();
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

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