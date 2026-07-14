/**
 * @file VirtualSerial.cpp
 * @brief 豆子分拣串口通信类实现
 * @author lxy
 * @date 2025-10-24
 *
 * 数据包格式 —— 匹配信号（4 字节）：
 *   帧头(0x5C) + 信号(1) + CRC16(2)
 *   信号固定为 1（匹配，执行抓取/放置操作）
 */

#include "VirtualSerial.h"
#include "CRC16.hpp"
#include <iostream>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iomanip>
#include <dirent.h>
#include <sys/stat.h>

// ============================================================
//  波特率 int → speed_t 转换
// ============================================================
static speed_t getBaudRateConstant(int baud)
{
    switch (baud) {
        case 0:       return B0;
        case 50:      return B50;
        case 75:      return B75;
        case 110:     return B110;
        case 134:     return B134;
        case 150:     return B150;
        case 200:     return B200;
        case 300:     return B300;
        case 600:     return B600;
        case 1200:    return B1200;
        case 1800:    return B1800;
        case 2400:    return B2400;
        case 4800:    return B4800;
        case 9600:    return B9600;
        case 19200:   return B19200;
        case 38400:   return B38400;
        case 57600:   return B57600;
        case 115200:  return B115200;
        case 230400:  return B230400;
        case 460800:  return B460800;
        case 500000:  return B500000;
        case 576000:  return B576000;
        case 921600:  return B921600;
        case 1000000: return B1000000;
        case 1152000: return B1152000;
        case 1500000: return B1500000;
        case 2000000: return B2000000;
        case 2500000: return B2500000;
        case 3000000: return B3000000;
        case 3500000: return B3500000;
        case 4000000: return B4000000;
        default:      return B115200;
    }
}

VirtualSerial::VirtualSerial(const std::string &portName)
    : serialFd_(-1), portName_(portName)
{
}

VirtualSerial::~VirtualSerial()
{
    Close();
}

bool VirtualSerial::Open()
{
    if (IsOpen())
    {
        return true;
    }

    serialFd_ = open(portName_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (serialFd_ < 0)
    {
        std::cerr << "[VirtualSerial] Error: Failed to open serial port" << std::endl;
        return false;
    }

    if (!ConfigurePort())
    {
        Close();
        return false;
    }

    return true;
}

bool VirtualSerial::sendMatchSignal(int maxRetries)
{
    // 信号固定为 1
    const uint8_t signal = 1;

    if (simulated_)
    {
        if (txLogEnabled_)
            std::cout << "[VirtualSerial] Simulated TX MatchSignal: " << static_cast<int>(signal) << std::endl;
        return true;
    }

    if (!IsOpen())
    {
        std::cerr << "[VirtualSerial] Error : Serial port not open" << std::endl;
        return false;
    }

    // 帧格式：帧头(0x5C) + 信号(1) + CRC16(2) = 4 字节
    uint8_t frame[MATCH_PACKET_SIZE];
    frame[0] = FRAME_HEADER_MATCH;
    frame[1] = signal;

    // CRC16 填入前先置零
    frame[2] = 0x00;
    frame[3] = 0x00;
    crc16::Append_CRC16_Check_Sum(frame, MATCH_PACKET_SIZE);

    if (txLogEnabled_)
    {
        std::cout << "[VirtualSerial] TX MatchSignal: " << static_cast<int>(signal) << "  frame =";
        std::ios oldState(nullptr);
        oldState.copyfmt(std::cout);
        for (size_t i = 0; i < MATCH_PACKET_SIZE; ++i)
            std::cout << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                      << static_cast<int>(frame[i]) << (i < MATCH_PACKET_SIZE - 1 ? " " : "");
        std::cout.copyfmt(oldState);
        std::cout << std::endl;
    }

    for (int retry = 0; retry < maxRetries; retry++)
    {
        if (write(serialFd_, frame, MATCH_PACKET_SIZE) == static_cast<ssize_t>(MATCH_PACKET_SIZE))
            return true;
        if (retry < maxRetries - 1)
            usleep(retryIntervalUs_);
    }

    std::cerr << "[VirtualSerial] Error : Failed to send match signal after " << maxRetries << " retries" << std::endl;

    if (autoReconnect_ && TryReconnect())
    {
        std::cout << "[VirtualSerial] Retrying send after reconnect..." << std::endl;
        if (write(serialFd_, frame, MATCH_PACKET_SIZE) == static_cast<ssize_t>(MATCH_PACKET_SIZE))
            return true;
    }

    return false;
}

bool VirtualSerial::sendDataPacket(const std::vector<uint8_t> &payload, int maxRetries)
{
    if (simulated_)
    {
        if (txLogEnabled_)
        {
            std::cout << "[VirtualSerial] Simulated TX DataPacket: payload =";
            std::ios oldState(nullptr);
            oldState.copyfmt(std::cout);
            for (size_t i = 0; i < payload.size(); ++i)
                std::cout << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                          << static_cast<int>(payload[i]) << (i + 1 < payload.size() ? " " : "");
            std::cout.copyfmt(oldState);
            std::cout << std::endl;
        }
        return true;
    }

    if (!IsOpen())
    {
        std::cerr << "[VirtualSerial] Error : Serial port not open" << std::endl;
        return false;
    }

    // 帧格式：帧头(0x5D) + 长度(1) + 有效载荷(n) + CRC16(2)
    size_t frameSize = 1 + 1 + payload.size() + 2; // header + len + payload + crc
    std::vector<uint8_t> frame(frameSize);
    frame[0] = FRAME_HEADER_DATA;
    frame[1] = static_cast<uint8_t>(payload.size() & 0xFF);
    if (!payload.empty())
        std::memcpy(&frame[2], payload.data(), payload.size());

    // CRC16 填入前先置零（两字节）
    frame[frameSize - 2] = 0x00;
    frame[frameSize - 1] = 0x00;
    crc16::Append_CRC16_Check_Sum(frame.data(), static_cast<int>(frameSize));

    if (txLogEnabled_)
    {
        std::cout << "[VirtualSerial] TX DataPacket: frame =";
        std::ios oldState(nullptr);
        oldState.copyfmt(std::cout);
        for (size_t i = 0; i < frameSize; ++i)
            std::cout << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                      << static_cast<int>(frame[i]) << (i < frameSize - 1 ? " " : "");
        std::cout.copyfmt(oldState);
        std::cout << std::endl;
    }

    for (int retry = 0; retry < maxRetries; retry++)
    {
        if (write(serialFd_, frame.data(), frameSize) == static_cast<ssize_t>(frameSize))
            return true;
        if (retry < maxRetries - 1)
            usleep(retryIntervalUs_);
    }

    std::cerr << "[VirtualSerial] Error : Failed to send data packet after " << maxRetries << " retries" << std::endl;

    if (autoReconnect_ && TryReconnect())
    {
        std::cout << "[VirtualSerial] Retrying send after reconnect..." << std::endl;
        if (write(serialFd_, frame.data(), frameSize) == static_cast<ssize_t>(frameSize))
            return true;
    }

    return false;
}

void VirtualSerial::Close()
{
    if (IsOpen())
    {
        close(serialFd_);
        serialFd_ = -1;
    }
}

bool VirtualSerial::ConfigurePort()
{
    struct termios tty;
    std::memset(&tty, 0, sizeof(tty));

    if (tcgetattr(serialFd_, &tty) != 0)
    {
        std::cerr << "[VirtualSerial] Error: Failed to get port attributes" << std::endl;
        return false;
    }

    // 波特率设置
    speed_t baudConst = getBaudRateConstant(baudRate_);
    cfsetispeed(&tty, baudConst);
    cfsetospeed(&tty, baudConst);

    // 控制模式标志
    tty.c_cflag &= ~PARENB; // 无校验
    tty.c_cflag &= ~CSTOPB; // 1个停止位
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;            // 8位数据位
    tty.c_cflag &= ~CRTSCTS;       // 禁用硬件流控
    tty.c_cflag |= CREAD | CLOCAL; // 启用接收器,忽略调制解调器状态线

    // 本地模式标志
    tty.c_lflag &= ~ICANON; // 原始模式
    tty.c_lflag &= ~ECHO;   // 禁用回显
    tty.c_lflag &= ~ECHOE;
    tty.c_lflag &= ~ECHONL;
    tty.c_lflag &= ~ISIG; // 禁用信号

    // 输入模式标志
    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // 禁用软件流控
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    // 输出模式标志
    tty.c_oflag &= ~OPOST; // 原始输出
    tty.c_oflag &= ~ONLCR;

    // 应用设置
    if (tcsetattr(serialFd_, TCSANOW, &tty) != 0)
    {
        std::cerr << "[VirtualSerial] Error: Failed to set port attributes" << std::endl;
        return false;
    }

    // 清空缓冲区
    tcflush(serialFd_, TCIOFLUSH);

    return true;
}

std::string VirtualSerial::FindAvailablePort()
{
    DIR *dir = opendir("/dev");
    if (!dir)
        return "";

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        std::string name = entry->d_name;
        if (name.find("ttyACM") == 0 || name.find("ttyUSB") == 0)
        {
            std::string fullPath = "/dev/" + name;
            int testFd = open(fullPath.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
            if (testFd >= 0)
            {
                close(testFd);
                closedir(dir);
                return fullPath;
            }
        }
    }
    closedir(dir);
    return "";
}

bool VirtualSerial::TryReconnect()
{
    if (!autoReconnect_)
        return false;

    Close();

    // 等待设备节点重新出现（MCU 复位后 USB 重新枚举需要时间）
    std::string newPort;
    for (int waited = 0; waited < reconnectMaxWaitMs_; waited += reconnectIntervalMs_)
    {
        newPort = FindAvailablePort();
        if (!newPort.empty())
            break;
        usleep(reconnectIntervalMs_ * 1000);
    }

    if (newPort.empty())
    {
        std::cerr << "[VirtualSerial] Reconnect: No available port found" << std::endl;
        return false;
    }

    if (newPort != portName_)
    {
        std::cout << "[VirtualSerial] Reconnect: Port changed from " << portName_ << " to " << newPort << std::endl;
        portName_ = newPort;
    }

    if (Open())
    {
        std::cout << "[VirtualSerial] Reconnect: Successfully reconnected to " << portName_ << std::endl;
        return true;
    }

    return false;
}
