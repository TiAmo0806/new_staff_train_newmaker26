/**
 * serial.cpp —— 串口通信类实现（Linux termios）
 *   - 原始字节发送
 *   - 数据包发送（自动 CRC16 + 重试 + 重连 + TX 日志）
 */

#include "serial.hpp"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <iostream>
#include <iomanip>
#include <cstring>
#include <thread>
#include <chrono>

SerialPort::SerialPort() : fd_(-1) {}

SerialPort::~SerialPort() {
    close();
}

bool SerialPort::open(const std::string& port) {
    if (isOpen()) {
        close();
    }
    portName_ = port;
    /*
    O_RDWR：“读写模式”。设置既能往这个设备写数据，也能从它读数据。这是串口通信的标配。
    O_NOCTTY：“不成为控制终端”。如果省略这个标志，并且这个串口恰好是 tty 类型的设备，
    它可能会成为当前进程的控制终端，导致 Ctrl+C 等信号干扰你的程序运行。
    O_NDELAY：“非阻塞打开”。即使串口设备没有准备好，open 函数也绝不等待，立刻返回。
    如果不用这个标志，某些串口在打开时会一直阻塞直到硬件就绪，主程序就卡死了。
    注意：虽然打开是非阻塞的，但后面的读写操作通常需要用 fcntl 再单独设置成阻塞模式（除非特意要搞非阻塞读写）。
    这里只是保证“打开”这一步不卡死。
    |：按位或运算符。因为 O_RDWR、O_NOCTTY、O_NDELAY 是三个互不重叠的二进制位，
    用 | 把它们“拼”成一个最终的标志位掩码传给内核。
    */
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
    //清理串口垃圾数据
    std::cout << "SerialPort: 成功打开 " << port << " @ 115200bps" << std::endl;
    return true;
}

bool SerialPort::configurePort() {
    /*
    Linux 系统（POSIX 标准）里定义的一个巨大结构体，专门用来存放终端（包括串口）的所有配置参数。
    里面包含了：输入模式标志（c_iflag）,输出模式标志（c_oflag）,控制模式标志（c_cflag）,
    本地模式标志（c_lflag）,特殊字符数组（c_cc，比如 Ctrl+C、Ctrl+Z 对应的值）,波特率（速度）
    */
    struct termios tty;
    //<cstring>的函数。它把从某个地址开始、指定长度的内存区域，全部填充为指定的字节值
    std::memset(&tty, 0, sizeof(tty));
    //向内核询问"当前这个串口在用的合法配置是什么"，把内核认可的基础合理值填回 tty
    if (tcgetattr(fd_, &tty) != 0) {
        std::cerr << "SerialPort: tcgetattr 失败" << std::endl;
        return false;
    }

    cfsetispeed(&tty, B115200);//输入波特率
    cfsetospeed(&tty, B115200);//输出波特率

    // 控制模式：8N1，无硬件流控
    tty.c_cflag &= ~PARENB;   // 无校验位
    tty.c_cflag &= ~CSTOPB;   // 1位停止位
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;       // 8位数据位
    tty.c_cflag &= ~CRTSCTS;  // 禁用硬件流控
    tty.c_cflag |= CREAD | CLOCAL;

    // 本地模式：原始输入，把规范模式的所有功能全部关闭
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

    if (tcsetattr(fd_, TCSANOW, &tty) != 0)
    //表示配置更改立即生效，不等待当前正在传输的数据发送完毕。与之相对的是 TCSADRAIN
    //tcsetattr 成功返回 0，失败返回 -1
    {
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
        std::cerr << "SerialPort: 写入失败，期望 " << len
                  << " 字节，实际写入 " << written << std::endl;
        return false;
    }
    return true;
}

//  数据包发送（重试 + 重连 + TX 日志）
bool SerialPort::sendPacket(const std::vector<uint8_t>& data,
                            int maxRetries) {
    // 1. TX 调试日志
    if (txLogEnabled_) {
        std::cout << "[SerialPort] TX ("
                  << data.size() << "B): ";
        //清晰的模块标签，包含这条日志来自哪个模块（SerialPort），以及方向是发送（TX，Transmit）。
        std::ios oldState(nullptr);
        oldState.copyfmt(std::cout);
        /*                                                                
        把 std::cout 当前的所有格式设置（如数字进制、宽度、填充字符、大小写等）完整复制一份到 oldState 里                                          
        用于使用std::hex（十六进制）和 std::setw（宽度）来格式化输出之后复原格式                                                                  
        */
        for (size_t i = 0; i < data.size(); ++i)
            std::cout << std::uppercase << std::hex << std::setw(2)
                      << std::setfill('0')
                      << static_cast<int>(data[i])
                      << (i < data.size() - 1 ? " " : "");
        std::cout.copyfmt(oldState);//恢复流格式
        std::cout << std::dec << std::endl;
    }

    // 2. 发送 + 重试
    for (int retry = 0; retry < maxRetries; ++retry) {
        if (send(data.data(), data.size()))
            return true;
        if (retry < maxRetries - 1)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::cerr << "[SerialPort] Error: 发送失败 (" << maxRetries
              << " 次重试)" << std::endl;

    // 3. 自动重连并再试一次
    if (autoReconnect_ && tryReconnect()) {
        std::cout << "[SerialPort] 重连成功，重试发送..." << std::endl;
        return send(data.data(), data.size());
    }

    return false;
}

bool SerialPort::tryReconnect() {
    std::cerr << "[SerialPort] 尝试重连 " << portName_ << " ..." << std::endl;
    close();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    return open(portName_);
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
