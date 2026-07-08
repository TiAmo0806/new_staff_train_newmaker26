#include "/home/zst/zst/include/Communication/VirtualSerial.h"
#include "/home/zst/zst/include/Communication/CRC16.hpp"
#include <dirent.h>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <termios.h>
#include <unistd.h>

VirtualSerial::VirtualSerial(const SerialConfig &config) : config_(config) {}

VirtualSerial::~VirtualSerial()
{
    closePort();
}

bool VirtualSerial::openPort()
{
    if (config_.simulated) return true;
    fd_ = open(config_.port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) return false;
    return configurePort();
}

void VirtualSerial::closePort()
{
    if (fd_ >= 0)
    {
        close(fd_);
        fd_ = -1;
    }
}

bool VirtualSerial::isOpen() const
{
    return config_.simulated || fd_ >= 0;
}

bool VirtualSerial::sendPacket(const VisionTxPacket &packet, int maxRetries)
{
    // 当前协议占位：帧头 + payload + CRC16。
    // 例：payload 为空时，发送 3 字节：
    //   A6 CRC_L CRC_H
    // 这样可以先验证“程序能不能持续往串口发数据”。
    std::vector<uint8_t> frame;
    frame.reserve(packet.payload.size() + 3);
    frame.push_back(FRAME_HEADER);
    frame.insert(frame.end(), packet.payload.begin(), packet.payload.end());
    frame.push_back(0);
    frame.push_back(0);
    crc16::Append(frame.data(), static_cast<int>(frame.size()));

    if (config_.txLog)
    {
        // 调试时打印完整十六进制帧。
        std::cout << "[Serial] TX";
        for (uint8_t b : frame)
            std::cout << " " << std::hex << std::uppercase << std::setw(2)
                      << std::setfill('0') << static_cast<int>(b);
        std::cout << std::dec << std::endl;
    }

    if (config_.simulated) return true;
    if (fd_ < 0 && !tryReconnect()) return false;

    for (int i = 0; i < maxRetries; ++i)
    {
        if (write(fd_, frame.data(), frame.size()) == static_cast<ssize_t>(frame.size()))
            return true;
        usleep(1000);
    }
    return tryReconnect() && write(fd_, frame.data(), frame.size()) == static_cast<ssize_t>(frame.size());
}

bool VirtualSerial::configurePort()
{
    termios tty{};
    if (tcgetattr(fd_, &tty) != 0) return false;
    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8 | CREAD | CLOCAL;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHONL | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL);
    tty.c_oflag &= ~OPOST;
    return tcsetattr(fd_, TCSANOW, &tty) == 0;
}

std::string VirtualSerial::findAvailablePort() const
{
    DIR *dir = opendir("/dev");
    if (!dir) return "";
    dirent *entry = nullptr;
    while ((entry = readdir(dir)) != nullptr)
    {
        std::string name = entry->d_name;
        if (name.find("ttyACM") == 0 || name.find("ttyUSB") == 0)
        {
            closedir(dir);
            return "/dev/" + name;
        }
    }
    closedir(dir);
    return "";
}

bool VirtualSerial::tryReconnect()
{
    closePort();
    std::string port = findAvailablePort();
    if (port.empty()) return false;
    config_.port = port;
    return openPort();
}
