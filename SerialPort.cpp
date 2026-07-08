#include "../include/SerialPort.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <iostream>
#include <chrono>
#include <thread>

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
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

bool SerialPort::sendPacket(const rm_serial_driver::VisionToMCUPacket& packet) {
    if (fd < 0) return false;
    
    std::vector<uint8_t> data = rm_serial_driver::toVector(packet);
    ssize_t sent = write(fd, data.data(), data.size());
    if (sent < 0) return false;
    
    std::cout << "[Serial] Send: header=0xA6 target_type=" << (int)packet.target_type
              << " target_id=" << (int)packet.target_id
              << " tracking=" << packet.tracking
              << " pitch=" << packet.pitch_cmd 
              << " yaw=" << packet.yaw_cmd << std::endl;
    return true;
}

bool SerialPort::send(const std::string& data) {
    if (fd < 0) return false;
    ssize_t sent = write(fd, data.c_str(), data.length());
    return sent >= 0;
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