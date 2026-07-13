/**
 * @file VirtualSerial.h
 * @brief 豆子分拣串口通信类
 * @author lxy
 * @date 2025-10-24
 *
 * 数据包格式 —— 匹配信号（4 字节）：
 *   帧头(0x5C) + 信号(1) + CRC16(2)
 *   信号：1 = 匹配，执行抓取/放置操作
 */

#ifndef VIRTUAL_SERIAL_H
#define VIRTUAL_SERIAL_H

#include <string>
#include <cstdint>
#include <vector>

class VirtualSerial
{
public:
    // 帧格式常量
    static const uint8_t FRAME_HEADER_MATCH  = 0x5C;  // 匹配信号包帧头
    static const size_t  MATCH_PACKET_SIZE   = 4;     // 匹配信号包总字节数

    /**
     * @brief 构造函数
     * @param portName 串口设备名称，默认为"/dev/ttyACM0"
     */
    explicit VirtualSerial(const std::string &portName = "/dev/ttyACM0");

    /**
     * @brief 析构函数
     */
    ~VirtualSerial();

    // 禁用拷贝
    VirtualSerial(const VirtualSerial &) = delete;
    VirtualSerial &operator=(const VirtualSerial &) = delete;

    /**
     * @brief 打开串口
     * @return 是否成功
     */
    bool Open();

    /**
     * @brief 关闭串口
     */
    void Close();

    /**
     * @brief 发送匹配信号给电控（带重试机制）
     *
     * 帧格式：帧头(0x5C) + 信号(1) + CRC16(2) = 4 字节
     * 信号固定为 1（匹配，执行抓取/放置操作）
     *
     * @param maxRetries 最大重试次数，默认3次
     * @return 是否成功
     */
    bool sendMatchSignal(int maxRetries = 3);

    /**
     * @brief 检查串口是否已打开
     * @return 是否已打开
     */
    bool IsOpen() const { return serialFd_ >= 0; }

    /**
     * @brief 设置是否为模拟模式（无串口运行）
     * @param simulated 是否启用模拟模式
     */
    void SetSimulated(bool simulated) { simulated_ = simulated; }

    /**
     * @brief 设置是否在终端输出发送到电控端的值
     * @param enabled true=输出，false=关闭
     */
    void SetTxLogEnabled(bool enabled) { txLogEnabled_ = enabled; }

    /**
     * @brief 检查是否开启发送日志输出
     * @return 是否开启
     */
    bool IsTxLogEnabled() const { return txLogEnabled_; }

    /**
     * @brief 检查是否处于模拟模式
     * @return 是否处于模拟模式
     */
    bool IsSimulated() const { return simulated_; }

    /**
     * @brief 获取串口设备名称
     * @return 设备名称
     */
    const std::string &GetPortName() const { return portName_; }

    /**
     * @brief 启用自动重连功能
     * @param enabled true=启用自动重连，false=禁用
     */
    void SetAutoReconnect(bool enabled) { autoReconnect_ = enabled; }

    /**
     * @brief 尝试重新连接串口（扫描可用设备）
     * @return 是否成功重连
     */
    bool TryReconnect();

    /**
     * @brief 设置串口波特率（下次 Open 时生效）
     * @param baudRate 波特率 (如 9600, 115200, 921600)
     */
    void SetBaudRate(int baudRate) { baudRate_ = baudRate; }

    /**
     * @brief 设置发送重试间隔（微秒）
     * @param intervalUs 重试间隔，默认 1000
     */
    void SetRetryIntervalUs(int intervalUs) { retryIntervalUs_ = intervalUs; }

    /**
     * @brief 设置自动重连参数
     * @param maxWaitMs  最大等待时间（毫秒）
     * @param intervalMs 轮询间隔（毫秒）
     */
    void SetReconnectParams(int maxWaitMs, int intervalMs) {
        reconnectMaxWaitMs_  = maxWaitMs;
        reconnectIntervalMs_ = intervalMs;
    }

private:
    /**
     * @brief 配置串口参数
     * @return 是否成功
     */
    bool ConfigurePort();

    /**
     * @brief 扫描可用的串口设备（/dev/ttyACM*）
     * @return 找到的设备名称，未找到返回空字符串
     */
    std::string FindAvailablePort();

    int serialFd_;              // 串口文件描述符
    std::string portName_;      // 串口设备名称
    bool simulated_ = false;    // 模拟模式标志
    bool txLogEnabled_ = false; // 是否输出发送日志
    bool autoReconnect_ = true; // 自动重连标志
    int  baudRate_            = 115200;   // 串口波特率
    int  retryIntervalUs_     = 1000;     // 发送重试间隔（微秒）
    int  reconnectMaxWaitMs_  = 5000;     // 重连最大等待（毫秒）
    int  reconnectIntervalMs_ = 200;      // 重连轮询间隔（毫秒）
};

#endif // VIRTUAL_SERIAL_H
