#include "Communication/VirtualSerial.h"
#include "Communication/CRC16.hpp"
#include <algorithm>
#include <cerrno>
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

bool VirtualSerial::receiveCameraState(uint8_t &cameraState)
{
    // 模拟模式没有真实电控输入，直接返回“当前没有新命令”。
    // 这样只做算法调试时仍保持相机默认开启，不会因为等电控而停住。
    if (config_.simulated) return false;

    // 如果串口启动时未连接，或运行中读串口出错被关闭，则按1秒间隔自动重连。
    // 不能在每帧都重连，否则相机关闭等待期间会持续扫描/dev并输出大量日志。
    if (fd_ < 0)
    {
        const auto now = std::chrono::steady_clock::now();
        if (now < nextReceiveReconnectAttempt_) return false;
        nextReceiveReconnectAttempt_ = now + std::chrono::seconds(1);
        if (!tryReconnect()) return false;
    }

    // 串口以O_NONBLOCK打开。一次read可能得到0字节、半帧、一帧或多帧，
    // 因此先尽量把当前内核缓冲区中的字节读入receiveBuffer_，再统一拆帧。
    uint8_t chunk[64]{};
    while (true)
    {
        const ssize_t received = read(fd_, chunk, sizeof(chunk));
        if (received > 0)
        {
            receiveBuffer_.insert(receiveBuffer_.end(), chunk, chunk + received);
            continue;                                      // 继续读取当前已到达的剩余字节
        }
        if (received == 0) break;                           // 当前没有更多数据
        if (errno == EINTR) continue;                       // 被信号打断，重新执行read
        if (errno == EAGAIN || errno == EWOULDBLOCK) break; // 非阻塞串口暂时无数据

        // EIO、ENODEV等通常表示USB串口掉线。关闭旧fd，下一轮按上面的节流逻辑重连。
        std::cerr << "[Serial] 接收失败，准备自动重连，errno=" << errno << std::endl;
        closePort();
        receiveBuffer_.clear();                             // 丢弃断线前不完整的旧帧
        return false;
    }

    // 防止线路噪声长期没有合法帧头时让缓存无限增长。只保留最近256字节，
    // 后续仍会从其中搜索0x5A并自动恢复帧边界。
    constexpr size_t MAX_RECEIVE_BUFFER_SIZE = 256;
    if (receiveBuffer_.size() > MAX_RECEIVE_BUFFER_SIZE)
    {
        receiveBuffer_.erase(
            receiveBuffer_.begin(),
            receiveBuffer_.end() - static_cast<std::ptrdiff_t>(MAX_RECEIVE_BUFFER_SIZE));
    }

    while (true)
    {
        // 在连续字节流中寻找电控->视觉帧头。帧头前的噪声或上一帧残片直接丢弃。
        const auto header = std::find(receiveBuffer_.begin(), receiveBuffer_.end(),
                                      RX_FRAME_HEADER);
        if (header == receiveBuffer_.end())
        {
            receiveBuffer_.clear();                         // 当前缓存完全没有合法帧头
            return false;
        }
        receiveBuffer_.erase(receiveBuffer_.begin(), header);

        // 已找到0x5A但后续状态/CRC尚未全部到达，保留半帧等待下一次调用。
        if (receiveBuffer_.size() < CAMERA_CONTROL_FRAME_SIZE) return false;

        // CRC覆盖[0x5A][camera_state]，线路按低字节、高字节发送CRC结果。
        if (!crc16::Verify(receiveBuffer_.data(),
                           static_cast<int>(CAMERA_CONTROL_FRAME_SIZE)))
        {
            std::cerr << "[Serial] RX相机控制帧CRC错误，丢弃当前0x5A并继续找下一帧"
                      << std::endl;
            receiveBuffer_.erase(receiveBuffer_.begin());   // 只丢帧头，尽量保留后续有效数据
            continue;
        }

        const uint8_t parsedState = receiveBuffer_[1];

        if (config_.rxLog)
        {
            std::cout << "[Serial] RX";
            for (size_t i = 0; i < CAMERA_CONTROL_FRAME_SIZE; ++i)
                std::cout << " " << std::hex << std::uppercase << std::setw(2)
                          << std::setfill('0') << static_cast<int>(receiveBuffer_[i]);
            std::cout << std::dec << "，camera_state="
                      << static_cast<int>(parsedState) << std::endl;
        }

        // 当前完整帧已经消费。若一次read收到了多帧，其余字节留给下一次调用解析。
        receiveBuffer_.erase(receiveBuffer_.begin(),
                             receiveBuffer_.begin() +
                                 static_cast<std::ptrdiff_t>(CAMERA_CONTROL_FRAME_SIZE));

        // 业务层目前只定义0/1。CRC虽然正确，但其他状态值也必须拒绝，避免误操作相机。
        if (parsedState > 1)
        {
            std::cerr << "[Serial] 非法camera_state=" << static_cast<int>(parsedState)
                      << "，只允许0或1，本帧已忽略" << std::endl;
            continue;
        }

        cameraState = parsedState;                         // 把合法命令交给main处理
        return true;
    }
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
