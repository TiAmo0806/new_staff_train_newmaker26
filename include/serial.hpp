#ifndef SERIAL_HPP_
#define SERIAL_HPP_

#include <string>
#include <cstdint>
#include <vector>

class SerialPort {
public:
    SerialPort();
    ~SerialPort();

    bool open(const std::string& port);
    bool send(const uint8_t* data, size_t len);

    /// 发送字节数组（自动重试、重连、TX 日志）
    bool sendPacket(const std::vector<uint8_t>& data,
                    int maxRetries = 3);

    void close();
    bool isOpen() const;

    /// 调试：开关 TX 日志（默认开）
    void setTxLogEnabled(bool enabled) { txLogEnabled_ = enabled; }

    /// 自动重连（默认开）
    void setAutoReconnect(bool enabled) { autoReconnect_ = enabled; }

private:
    bool configurePort();
    bool tryReconnect();

    int fd_;
    std::string portName_;          // 记住端口名，用于重连
    bool txLogEnabled_   = true;    // TX 调试日志
    //txLogEnabled_：这是一个布尔类型的成员变量（通常在类构造时由配置文件或命令行参数设置）。
    //true 表示“打印发送日志”，false 表示“静默发送，不打印任何调试信息”。
    bool autoReconnect_  = true;    // 发送失败时尝试重连
};

#endif
