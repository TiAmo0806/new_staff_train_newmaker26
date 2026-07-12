/**
 * @file VirtualSerial.h
 * @brief 飞镖制导串口通信类（集成 mvs_openvino_demo）
 * @author lxy
 * @date 2025-10-24
 */

#ifndef VIRTUAL_SERIAL_H
#define VIRTUAL_SERIAL_H

#include <string>
#include <cstdint>
#include <vector>

/**
 * @brief 飞镖制导串口通信类
 * 提供与电控系统的串口通信功能
 */
class VirtualSerial
{
public:
    // 帧格式常量
    static const uint8_t FRAME_HEADER = 0x5A;      // 帧头标识（控制指令）
    static const uint8_t ORDER_HEADER = 0xA5;      // 帧头标识（检测排序结果）

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
     * @brief 发送浮点数控制指令（带重试机制）
     * @param turnFlag 转向标志（0=停止转动, 1=向左转, 2=向右转）
     * @param maxRetries 最大重试次数，默认3次
     * @return 是否成功
     */
    bool sendCommand(float turnFlag, float distanceFlag = 0.0f, float clearFlag = 1.0f,
                     int maxRetries = 3);

    /**
     * @brief 发送检测物品从左到右的排序结果
     * @param classIds  按 X 坐标从左到右排序后的类别 ID 列表（如 {1, 3, 4}）
     * @param maxRetries 最大重试次数，默认3次
     * @return 是否成功
     *
     * 帧格式：ORDER_HEADER(0xA5) + count(1B) + [classId0, classId1, ...] + CRC16(2B)
     * 总长度 = 1 + 1 + N + 2 = 4 + N 字节
     */
    bool sendDetectionOrder(const std::vector<int>& classIds, int maxRetries = 3);

    /**
     * @brief 检查串口是否已打开
     */
    bool IsOpen() const { return serialFd_ >= 0; }

    /**
     * @brief 设置是否为模拟模式（无串口运行）
     */
    void SetSimulated(bool simulated) { simulated_ = simulated; }

    /**
     * @brief 设置是否输出发送日志
     */
    void SetTxLogEnabled(bool enabled) { txLogEnabled_ = enabled; }

    /**
     * @brief 是否开启发送日志
     */
    bool IsTxLogEnabled() const { return txLogEnabled_; }

    /**
     * @brief 是否处于模拟模式
     */
    bool IsSimulated() const { return simulated_; }

    /**
     * @brief 获取串口设备名称
     */
    const std::string &GetPortName() const { return portName_; }

    /**
     * @brief 启用自动重连功能
     */
    void SetAutoReconnect(bool enabled) { autoReconnect_ = enabled; }

    /**
     * @brief 尝试重新连接串口
     */
    bool TryReconnect();

private:
    bool ConfigurePort();
    std::string FindAvailablePort();

    int serialFd_;
    std::string portName_;
    bool simulated_ = false;
    bool txLogEnabled_ = false;
    bool autoReconnect_ = true;
};

#endif // VIRTUAL_SERIAL_H
