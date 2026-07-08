#ifndef VIRTUAL_SERIAL_H
#define VIRTUAL_SERIAL_H

#include <cstdint>
#include <string>
#include <vector>

struct SerialConfig
{
    // Linux 下常见串口名：/dev/ttyACM0 或 /dev/ttyUSB0。
    std::string port = "/dev/ttyACM0";

    // 当前代码固定按 115200 配置。
    int baudrate = 115200;

    // true 表示不真正写串口，只打印帧，方便没接电控时调视觉。
    bool simulated = true;

    // true 表示打印十六进制发送帧。
    bool txLog = true;
};

struct VisionTxPacket
{
    // 电控协议未定，先保留 payload 占位。
    // 后续只需要把识别状态、目标编号、偏差量等塞进这里。
    std::vector<uint8_t> payload;
};

class VirtualSerial
{
public:
    // 算法发给电控的帧头，后续可和电控统一修改。
    static constexpr uint8_t FRAME_HEADER = 0xA6;

    explicit VirtualSerial(const SerialConfig &config);
    ~VirtualSerial();

    bool openPort();
    void closePort();
    bool isOpen() const;

    // 打包为：帧头 + payload + CRC16。
    // payload 为空也会发送一帧，说明通信流程是通的。
    // 后续协议确定后，只改 VisionTxPacket 的 payload 内容即可。
    bool sendPacket(const VisionTxPacket &packet, int maxRetries = 3);

private:
    bool configurePort();
    std::string findAvailablePort() const;
    bool tryReconnect();

    SerialConfig config_;
    int fd_ = -1;
};

#endif // VIRTUAL_SERIAL_H
