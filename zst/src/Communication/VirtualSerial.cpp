#include "Communication/VirtualSerial.h"
#include "Communication/CRC16.hpp"
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
    // 模拟模式把“串口可用”视为 true，使上层视觉流程可脱离电控板独立调试。
    if (config_.simulated) return true;
    // O_NOCTTY：该设备不会成为进程控制终端；O_NONBLOCK：打开和写入均不永久阻塞主循环。
    fd_ = open(config_.port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) return false;
    if (!configurePort())
    {
        closePort();
        return false;
    }
    return true;
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
    // 完整协议：A6 + VERSION/TEAM/TYPE/SESSION/SEQ/LEN/DATA + CRC16。
    std::vector<uint8_t> frame;
    frame.reserve(packet.payload.size() + 3);
    frame.push_back(FRAME_HEADER);
    frame.insert(frame.end(), packet.payload.begin(), packet.payload.end());
    // 先预留两个 CRC 字节；Append() 会基于前面的帧头和 payload 计算并覆盖它们。
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

    // 非阻塞串口可能暂时写不进去，因此短暂重试。这里要求一次 write 写完整帧；
    // 若将来 payload 很长，应改成循环处理“部分写入”，防止半帧被当作失败后重复发送。
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
    // 本项目使用 115200-8-N-1：115200 波特、8 数据位、无校验、1 停止位，
    // 同时关闭硬件/软件流控和终端行编辑，保证二进制字节原样传输。
    termios tty{};
    if (tcgetattr(fd_, &tty) != 0) return false;
    // 当前实现固定为 B115200；config_.baudrate 仅保留为后续扩展配置项。
    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);
    tty.c_cflag &= ~PARENB;  // 无奇偶校验（N）
    tty.c_cflag &= ~CSTOPB;  // 1 个停止位（1）
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8 | CREAD | CLOCAL; // 8 数据位，允许接收，忽略调制解调器控制线
    tty.c_cflag &= ~CRTSCTS;
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHONL | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL);
    tty.c_oflag &= ~OPOST;
    return tcsetattr(fd_, TCSANOW, &tty) == 0;
}

std::string VirtualSerial::findAvailablePort() const
{
    // 重连时扫描常见 USB CDC（ttyACM）和 USB 转串口（ttyUSB）设备。
    // 如果系统同时连接多个串口，这里只返回目录枚举到的第一个，实车应优先固定设备规则。
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
    // 原端口失效后重新扫描 /dev，并用新端口走完整 open + termios 配置流程。
    closePort();
    std::string port = findAvailablePort();
    if (port.empty()) return false;
    config_.port = port;
    return openPort();
}
