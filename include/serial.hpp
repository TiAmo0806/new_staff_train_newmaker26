#ifndef SERIAL_HPP_
#define SERIAL_HPP_

#include <string>
#include <cstdint>
#include <vector>
#include <chrono>

class SerialPort {
public:
    SerialPort();
    ~SerialPort();

    bool open(const std::string& port);
    bool send(const uint8_t* data, size_t len);

    /// 发送字节数组（自动重试、重连、TX 日志）
    bool transmit(const std::vector<uint8_t>& data,
                  int maxRetries = 3);

    void close();
    bool isOpen() const;

    /// 调试：开关 TX 日志（默认开）
    void setTxLogEnabled(bool enabled) { txLogEnabled_ = enabled; }

    /// 自动重连（默认开）
    void setAutoReconnect(bool enabled) { autoReconnect_ = enabled; }

    /// 设置重连冷却时间（ms）
    void setReconnectCooldownMs(int ms) { reconnectCooldownMs_ = ms; }

    /// 设置最大连续重连次数（超限后自动关闭重连）
    void setMaxReconnectAttempts(int n) { maxReconnectAttempts_ = n; }

private:
    bool configurePort();
    bool tryReconnect();

    int fd_;
    std::string portName_;          // 记住端口名，用于重连
    bool txLogEnabled_   = true;    // TX 调试日志
    //txLogEnabled_：这是一个布尔类型的成员变量（通常在类构造时由配置文件或命令行参数设置）。
    //true 表示”打印发送日志”，false 表示”静默发送，不打印任何调试信息”。
    bool autoReconnect_  = true;    // 发送失败时尝试重连

    // 重连状态（运行时）
    std::chrono::steady_clock::time_point lastReconnectAttempt_{};
    int  reconnectFailCount_ = 0;

    // 重连阈值（由 YAML 配置注入）
    int  reconnectCooldownMs_  = 5000;
    int  maxReconnectAttempts_ = 10;
};

#endif
