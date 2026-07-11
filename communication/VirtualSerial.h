/**
 * @file VirtualSerial.h
 * @brief 豆子分拣串口通信类
 * @author lxy
 * @date 2025-10-24
 *
 * 数据包格式 —— 豆子位置 + 对应数字箱号（9 字节）：
 *   帧头(1) + 左位[豆类别(1)+数字箱号(1)] + 中位[豆类别(1)+数字箱号(1)] + 右位[豆类别(1)+数字箱号(1)] + CRC16(2)
 *
 *   豆类别：0=黄豆, 1=绿豆, 2=白芸豆, 0xFF=无
 *   数字箱号：1=1号箱, 2=2号箱, 3=3号箱, 0=无
 *   电控端根据位置+类别决定抓取后放入对应数字箱：
 *     黄豆→1号箱  绿豆→2号箱  白芸豆→3号箱
 *
 * 数据包格式 —— 识别到的数字（4 字节）：
 *   帧头(0x5B) + 数字(1) + CRC16(2)
 *   数字：1~5 对应识别到的数字标签
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
    static const uint8_t FRAME_HEADER        = 0x5A;  // 帧头标识（豆子位置+数字箱号）
    static const uint8_t FRAME_HEADER_NUMBER = 0x5B;  // 帧头标识（识别到的数字）
    static const uint8_t FRAME_HEADER_MATCH  = 0x5C;  // 帧头标识（匹配信号 1/0）
    static const size_t  PACKET_SIZE         = 9;     // 豆子位置包总字节数
    static const size_t  NUMBER_PACKET_SIZE  = 4;     // 数字包总字节数
    static const size_t  MATCH_PACKET_SIZE   = 4;     // 匹配信号包总字节数

    // 豆类别常量（与 YOLO classId 一致）
    static constexpr uint8_t BEAN_SOYBEAN          = 0;    // 黄豆 → 1号箱
    static constexpr uint8_t BEAN_MUNG             = 1;    // 绿豆 → 2号箱
    static constexpr uint8_t BEAN_WHITE_KIDNEY     = 2;    // 白芸豆 → 3号箱
    static constexpr uint8_t BEAN_NONE             = 0xFF; // 无豆子

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
     * @brief 发送三厢豆子位置信息 + 对应数字箱号（带重试机制）
     * @param leftBean   左位豆子类别 (0=黄豆, 1=绿豆, 2=白芸豆, 0xFF=无)
     * @param midBean    中位豆子类别
     * @param rightBean  右位豆子类别
     * @param leftNum    左位对应数字箱号 (1/2/3, 0=无)
     * @param midNum     中位对应数字箱号 (1/2/3, 0=无)
     * @param rightNum   右位对应数字箱号 (1/2/3, 0=无)
     * @param maxRetries 最大重试次数，默认3次
     * @return 是否成功
     */
    bool sendBeanPosition(uint8_t leftBean, uint8_t midBean, uint8_t rightBean,
                          uint8_t leftNum = 0, uint8_t midNum = 0, uint8_t rightNum = 0,
                          int maxRetries = 3);

    /**
     * @brief 发送识别到的数字（带重试机制）
     * @param digit      识别到的数字 (1~5)
     * @param maxRetries 最大重试次数，默认3次
     * @return 是否成功
     */
    bool sendNumber(uint8_t digit, int maxRetries = 3);

    /**
     * @brief 发送匹配信号给电控（带重试机制）
     *
     * 用于大循环中判断识别数字是否与目标位置豆子数字匹配：
     *   1 = 匹配，执行抓取/放置操作
     *   0 = 不匹配
     *
     * 帧格式：帧头(0x5C) + 信号(1) + CRC16(2) = 4 字节
     *
     * @param signal     匹配信号 (1=匹配执行操作, 0=不匹配)
     * @param maxRetries 最大重试次数，默认3次
     * @return 是否成功
     */
    bool sendMatchSignal(uint8_t signal, int maxRetries = 3);

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