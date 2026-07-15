/**
 * @file VirtualSerial.h
 * @brief 串口通信类（双向通信：TX 发送检测结果 + RX 接收电控指令）
 * @author lxy
 * @date 2025-10-24
 */

#ifndef VIRTUAL_SERIAL_H
#define VIRTUAL_SERIAL_H

#include <string>
#include <cstdint>
#include <vector>
#include <functional>

/**
 * @brief 串口通信类
 *
 * 与电控 MCU 双向通信：
 * - TX：发送检测物品的从左到右排序结果（帧头 0xA5）
 * - RX：接收电控指令（帧头 0x5A，action: 0=停止, 1=开始）
 *
 * 使用方式：
 *   VirtualSerial serial;
 *   serial.Open();
 *   serial.SetRxCallback([](uint8_t action) {
 *       // action: 0=停止采集, 1=开始采集
 *   });
 *   // 主循环中：
 *   serial.PollReceive();                      // 非阻塞接收
 *   serial.sendDetectionOrder({1, 3, 4});     // 发送排序结果
 *   serial.Close();
 */
class VirtualSerial
{
public:
    /**
     * @brief 构造函数
     * @param portName  串口设备名，默认 "/dev/ttyACM0"
     */
    explicit VirtualSerial(const std::string &portName = "/dev/ttyACM0");

    ~VirtualSerial();

    // 禁止拷贝
    VirtualSerial(const VirtualSerial &) = delete;
    VirtualSerial &operator=(const VirtualSerial &) = delete;

    // ============================================================
    // 生命周期
    // ============================================================

    bool Open();
    void Close();
    bool IsOpen() const { return serialFd_ >= 0; }

    // ============================================================
    // 发送（唯一对外接口）
    // ============================================================

    /**
     * @brief 发送检测物品从左到右的排序结果
     *
     * @param classIds   按 X 坐标排序后的类别 ID 列表（如 {1, 3, 4}）
     * @param maxRetries 发送失败重试次数，默认 3
     * @return 是否发送成功
     *
     * 帧格式（由 packet.hpp 定义）：
     *   0xA5 + count(1B) + [classId...] + CRC16(2B)
     */
    bool sendDetectionOrder(const std::vector<int>& classIds, int maxRetries = 3);

    // ============================================================
    // 接收（双向通信 — MCU→PC）
    // ============================================================

    /// RX 回调类型：void(uint8_t action)
    /// action — 0=停止采集, 1=开始采集
    using RxCallback = std::function<void(uint8_t action)>;

    /// 设置 RX 回调（收到有效帧时调用）
    void SetRxCallback(RxCallback callback) { rxCallback_ = std::move(callback); }

    /**
     * @brief 非阻塞轮询接收（每帧调用一次，放在主循环中）
     *
     * 内部流程：
     *   1. read() 非阻塞读取串口，追加到 rxBuffer_
     *   2. ParseRxBuffer() 扫描 0xA5 帧头，提取完整帧
     *   3. HandleValidFrame() 校验 CRC → 调用 rxCallback_
     */
    void PollReceive();

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
    const std::string &GetPortName() const { return portName_; }

    /// 手动触发重连
    bool TryReconnect();

private:
    bool ConfigurePort();
    std::string FindAvailablePort();

    // ---- RX 解析 ----
    bool ParseRxBuffer();
    void HandleValidFrame(const uint8_t * frame);

    int serialFd_ = -1;
    std::string portName_;
    bool simulated_ = false;
    bool txLogEnabled_ = false;
    bool autoReconnect_ = true;

    // ---- RX 状态 ----
    RxCallback rxCallback_;
    std::vector<uint8_t> rxBuffer_;
};

#endif // VIRTUAL_SERIAL_H
