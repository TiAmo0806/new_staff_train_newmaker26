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
    // 模拟模式把"串口可用"视为 true，使上层视觉流程可脱离电控板独立调试。
    if (config_.simulated)
    {
        std::cout << "[Serial] 模拟模式已开启，不访问真实串口" << std::endl;
        return true;
    }
    // O_NOCTTY：该设备不会成为进程控制终端；O_NONBLOCK：打开和写入均不永久阻塞主循环。
    fd_ = open(config_.port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);  // 读写打开，非阻塞
    if (fd_ < 0)
    {
        std::cerr << "[Serial] 打开失败: " << config_.port << std::endl;
        return false;
    }
    if (!configurePort())                       // 配置 termios 参数
    {
        closePort();                            // 配置失败则关闭并返回
        std::cerr << "[Serial] 参数配置失败: " << config_.port << std::endl;
        return false;
    }
    std::cout << "[Serial] 已打开: " << config_.port
              << "，115200-8-N-1" << std::endl;
    return true;
}

void VirtualSerial::closePort()
{
    if (fd_ >= 0)
    {
        close(fd_);                             // 关闭 Linux 文件描述符
        fd_ = -1;                               // 标记为未打开
    }
}

bool VirtualSerial::isOpen() const
{
    return config_.simulated || fd_ >= 0;       // 模拟模式始终视为打开
}

bool VirtualSerial::sendPacket(const VisionTxPacket &packet, int maxRetries)
{
    // 最简协议：A6 + CMD + 固定长度DATA + CRC16。
    std::vector<uint8_t> frame;
    frame.reserve(packet.payload.size() + 3);                   // 预分配：载荷 + 帧头 + 2字节CRC
    frame.push_back(FRAME_HEADER);                              // [0] 帧头 0xA6
    frame.insert(frame.end(), packet.payload.begin(), packet.payload.end()); // [1..N] 协议载荷
    // 先预留两个 CRC 字节；Append() 会基于前面的帧头和 payload 计算并覆盖它们。
    frame.push_back(0);                                         // CRC 低字节占位
    frame.push_back(0);                                         // CRC 高字节占位
    crc16::Append(frame.data(), static_cast<int>(frame.size())); // 计算并填入 CRC

    if (config_.txLog)
    {
        // 调试时打印完整十六进制帧。
        std::cout << "[Serial] TX";
        for (uint8_t b : frame)
            std::cout << " " << std::hex << std::uppercase << std::setw(2)
                      << std::setfill('0') << static_cast<int>(b);  // 格式化为两位大写十六进制
        std::cout << std::dec << std::endl;                         // 恢复十进制输出
    }

    if (config_.simulated)
    {
        std::cout << "[Serial] 模拟发送完成，未写入硬件" << std::endl;
        return true;
    }
    if (fd_ < 0 && !tryReconnect())
    {
        std::cerr << "[Serial] 发送失败：串口未连接且自动重连失败" << std::endl;
        return false;
    }

    // 非阻塞串口可能暂时写不进去，因此短暂重试。这里要求一次 write 写完整帧；
    // 若将来 payload 很长，应改成循环处理"部分写入"，防止半帧被当作失败后重复发送。
    for (int i = 0; i < maxRetries; ++i)
    {
        if (write(fd_, frame.data(), frame.size()) == static_cast<ssize_t>(frame.size()))
        {
            std::cout << "[Serial] 实际发送成功，字节数=" << frame.size()
                      << "，尝试次数=" << (i + 1) << std::endl;
            return true;
        }
        usleep(1000);                               // 等待 1ms 后重试
    }
    const bool sent = tryReconnect() &&
        write(fd_, frame.data(), frame.size()) == static_cast<ssize_t>(frame.size());
    if (sent)
        std::cout << "[Serial] 重连后发送成功，字节数=" << frame.size() << std::endl;
    else
        std::cerr << "[Serial] 重试和重连后仍发送失败" << std::endl;
    return sent;
}

bool VirtualSerial::configurePort()
{
    // 本项目使用 115200-8-N-1：115200 波特、8 数据位、无校验、1 停止位，
    // 同时关闭硬件/软件流控和终端行编辑，保证二进制字节原样传输。
    termios tty{};
    if (tcgetattr(fd_, &tty) != 0) return false;    // 获取当前串口属性
    // 当前实现固定为 B115200；config_.baudrate 仅保留为后续扩展配置项。
    cfsetispeed(&tty, B115200);                     // 设置输入波特率为 115200
    cfsetospeed(&tty, B115200);                     // 设置输出波特率为 115200
    tty.c_cflag &= ~PARENB;                         // 无奇偶校验（N）
    tty.c_cflag &= ~CSTOPB;                         // 1 个停止位（1）
    tty.c_cflag &= ~CSIZE;                          // 清除数据位掩码
    tty.c_cflag |= CS8 | CREAD | CLOCAL;            // 8 数据位，允许接收，忽略调制解调器控制线
    tty.c_cflag &= ~CRTSCTS;                        // 关闭硬件流控（RTS/CTS）
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHONL | ISIG); // 关闭规范模式、回显和信号处理
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL); // 关闭软件流控、CR 转 NL
    tty.c_oflag &= ~OPOST;                          // 关闭输出处理，原始字节输出
    return tcsetattr(fd_, TCSANOW, &tty) == 0;      // 立即应用串口属性
}

std::string VirtualSerial::findAvailablePort() const
{
    // 重连时扫描常见 USB CDC（ttyACM）和 USB 转串口（ttyUSB）设备。
    // 如果系统同时连接多个串口，这里只返回目录枚举到的第一个，实车应优先固定设备规则。
    DIR *dir = opendir("/dev");                     // 打开 /dev 目录
    if (!dir) return "";                            // 打开失败
    dirent *entry = nullptr;
    while ((entry = readdir(dir)) != nullptr)       // 遍历目录条目
    {
        std::string name = entry->d_name;           // 获取文件名
        if (name.find("ttyACM") == 0 || name.find("ttyUSB") == 0)  // 匹配 ttyACM 或 ttyUSB 前缀
        {
            closedir(dir);                          // 找到后关闭目录
            return "/dev/" + name;                  // 返回完整设备路径
        }
    }
    closedir(dir);                                  // 遍历完毕，关闭目录
    return "";                                      // 未找到可用设备
}

bool VirtualSerial::tryReconnect()
{
    // 原端口失效后重新扫描 /dev，并用新端口走完整 open + termios 配置流程。
    closePort();                                    // 先关闭旧端口
    std::string port = findAvailablePort();         // 扫描可用设备
    if (port.empty())
    {
        std::cerr << "[Serial] 自动重连：未发现 /dev/ttyACM* 或 /dev/ttyUSB*" << std::endl;
        return false;
    }
    std::cout << "[Serial] 自动重连发现设备: " << port << std::endl;
    config_.port = port;                            // 更新配置中的端口名
    return openPort();                              // 用新端口重新打开
}
