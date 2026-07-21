/**
 * @file VirtualSerial.h
 * @brief 串口通信类（单向通信：TX 依次发送数字包 + 豆子包）
 * @author lxy
 * @date 2025-10-24
 */

#ifndef VIRTUAL_SERIAL_H
#define VIRTUAL_SERIAL_H

#include <string>
#include <cstdint>
#include <vector>

/**
 * @brief 串口通信类（纯 TX 单向发送）
 *
 * 与电控 MCU 单向通信，依次发送两个包：
 * - 数字包：header(0xA5) + 5 个数字 class_id + CRC16（固定 8 字节）
 * - 豆子包：header(0xA5) + 3 个豆子 class_id + CRC16（固定 6 字节）
 *
 * 使用方式：
 *   VirtualSerial serial;
 *   serial.Open();
 *   serial.sendDigitPacket({3, 4, 5, 6, 7});   // 发送数字包
 *   serial.sendBeanPacket({0, 1, 2});            // 发送豆子包
 *   serial.Close();
 */
class VirtualSerial
{
public:
    /**
     * @brief 构造函数
     * @param portName  串口设备名，默认 "/dev/ttyACM0"
     */
    explicit VirtualSerial(const std::string& portName = "/dev/ttyACM0");

    ~VirtualSerial();

    // 禁止拷贝
    VirtualSerial(const VirtualSerial&) = delete;
    VirtualSerial& operator=(const VirtualSerial&) = delete;

    // ============================================================
    // 生命周期
    // ============================================================

    bool Open();
    void Close();
    bool IsOpen() const { return serialFd_ >= 0; }

    // ============================================================
    // 发送（单向通信）
    // ============================================================

    /**
     * @brief 发送数字包（固定 8 字节）
     *
     * @param digitIds  5 个数字 class_id（3-7，按 X 坐标排序，
     *                  最后一个是推理补全的缺失数字）
     * @param maxRetries 发送失败重试次数，默认 3
     * @return 是否发送成功
     *
     * 帧格式：0xA5 + 5 digits + CRC16(2) = 固定 8 字节
     */
    bool sendDigitPacket(const std::vector<int>& digitIds, int maxRetries = 3);

    /**
     * @brief 发送豆子包（固定 6 字节）
     *
     * @param beanIds   3 个豆子 class_id（0-2，按 X 坐标排序，
     *                  最后一个是推理补全的缺失豆子）
     * @param maxRetries 发送失败重试次数，默认 3
     * @return 是否发送成功
     *
     * 帧格式：0xA5 + 3 beans + CRC16(2) = 固定 6 字节
     */
    bool sendBeanPacket(const std::vector<int>& beanIds, int maxRetries = 3);

    // ============================================================
    // 配置
    // ============================================================

    /// 模拟模式：不操作真实串口，仅打日志
    void SetSimulated(bool simulated) { simulated_ = simulated; }
    bool IsSimulated() const { return simulated_; }

    /// 发送日志：终端打印十六进制帧数据（调试用）
    void SetTxLogEnabled(bool enabled) { txLogEnabled_ = enabled; }
    bool IsTxLogEnabled() const { return txLogEnabled_; }

    /// 自动重连：断连后自动扫描 /dev 重新连接
    void SetAutoReconnect(bool enabled) { autoReconnect_ = enabled; }

    /// 获取当前串口设备路径
    const std::string& GetPortName() const { return portName_; }

    /// 手动触发重连
    bool TryReconnect();

private:
    bool ConfigurePort();
    std::string FindAvailablePort();

    /// 通用发送：写入帧数据 + 重试 + 重连
    bool sendFrame(const std::vector<uint8_t>& frame, int maxRetries);

    int serialFd_ = -1;
    std::string portName_;
    bool simulated_ = false;
    bool txLogEnabled_ = false;
    bool autoReconnect_ = true;
};

#endif // VIRTUAL_SERIAL_H
