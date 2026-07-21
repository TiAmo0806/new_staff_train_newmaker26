#ifndef BEAN_SORTING_SERIAL_H_
#define BEAN_SORTING_SERIAL_H_

#include <string>
#include <cstdint>
#include <vector>

class BeanSerial {
public:
    explicit BeanSerial(const std::string& portName = "/dev/ttyACM*");
    ~BeanSerial();

    BeanSerial(const BeanSerial&) = delete;
    BeanSerial& operator=(const BeanSerial&) = delete;

    bool Open();
    void Close();
    bool IsOpen() const { return fd_ >= 0; }
    void SetSimulated(bool v) { simulated_ = v; }
    bool IsSimulated() const { return simulated_; }
    void SetTxLogEnabled(bool v) { txLogEnabled_ = v; }
    bool IsTxLogEnabled() const { return txLogEnabled_; }
    void SetAutoReconnect(bool v) { autoReconnect_ = v; }
    bool TryReconnect();
    const std::string& GetPortName() const { return portName_; }

    // ---- 单向 TX: 对齐 mvs_openvino_demo 协议 ----
    /// 发送数字包 (固定8字节: 0xA5 + 5个class_id + CRC16)
    bool sendDigitPacket(const std::vector<int>& digitIds, int maxRetries = 3);

    /// 发送豆子包 (固定6字节: 0xA5 + 3个class_id + CRC16)
    bool sendBeanPacket(const std::vector<int>& beanIds, int maxRetries = 3);

private:
    bool ConfigurePort();
    std::string FindAvailablePort();
    bool sendFrame(const std::vector<uint8_t>& frame, int maxRetries);

    int fd_ = -1;
    std::string portName_;
    bool simulated_ = false;
    bool txLogEnabled_ = false;
    bool autoReconnect_ = true;
};

#endif
