/**
 * serial_port.cpp —— 串口通信类实现（Linux termios）
 */

#include "serial.hpp"
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <iostream>
#include <cstring>

SerialPort::SerialPort() : fd_(-1) {}

SerialPort::~SerialPort() {
    close();
}

bool SerialPort::open(const std::string& port) {
    if (isOpen()) {
        close();
    }

    fd_ = ::open(port.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd_ == -1) {
        std::cerr << "SerialPort: 无法打开 " << port << std::endl;
        return false;
    }

    if (!configurePort()) {
        close();
        return false;
    }

    tcflush(fd_, TCIOFLUSH);
    std::cout << "SerialPort: 成功打开 " << port << " @ 115200bps" << std::endl;
    return true;
}

bool SerialPort::configurePort() {
    struct termios tty;
    std::memset(&tty, 0, sizeof(tty));

    if (tcgetattr(fd_, &tty) != 0) {
        std::cerr << "SerialPort: tcgetattr 失败" << std::endl;
        return false;
    }

    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);

    // 控制模式：8N1，无硬件流控
    tty.c_cflag &= ~PARENB;   // 无校验位
    tty.c_cflag &= ~CSTOPB;   // 1位停止位
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;       // 8位数据位
    tty.c_cflag &= ~CRTSCTS;  // 禁用硬件流控
    tty.c_cflag |= CREAD | CLOCAL;

    // 本地模式：原始输入
    tty.c_lflag &= ~ICANON;
    tty.c_lflag &= ~ECHO;
    tty.c_lflag &= ~ECHOE;
    tty.c_lflag &= ~ECHONL;
    tty.c_lflag &= ~ISIG;

    // 输入模式：禁用软件流控
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    // 输出模式：原始输出
    tty.c_oflag &= ~OPOST;

    // 超时参数（非阻塞）
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
        std::cerr << "SerialPort: tcsetattr 失败" << std::endl;
        return false;
    }

    return true;
}

bool SerialPort::send(const uint8_t* data, size_t len) {
    if (!isOpen() || data == nullptr || len == 0) {
        return false;
    }

    ssize_t written = ::write(fd_, data, len);
    if (written != (ssize_t)len) {
        std::cerr << "SerialPort: 写入失败，期望 " << len << " 字节，实际写入 " << written << std::endl;
        return false;
    }
    return true;
}

void SerialPort::close() {
    if (isOpen()) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool SerialPort::isOpen() const {
    return fd_ >= 0;
}
