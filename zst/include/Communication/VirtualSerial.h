#ifndef VIRTUAL_SERIAL_H
#define VIRTUAL_SERIAL_H

#include <cstdint>
#include <string>
#include <vector>

struct SerialConfig
{
    // Linux 下常见串口名：/dev/ttyACM0 或 /dev/ttyUSB0。
    std::string port = "/dev/ttyACM0";

    // 期望波特率。注意：当前 configurePort() 只实现 115200，
    // 修改 YAML 中的数值不会自动改变 termios 常量，扩展其他波特率时需增加映射。
    int baudrate = 115200;

    // true：模拟模式，不打开 /dev 设备，也不真正发送，只按配置打印完整帧。
    // false：真实模式，程序会按 port 打开串口并执行 write()。
    bool simulated = true;

    // true 表示打印十六进制发送帧。
    bool txLog = true;
};

struct VisionTxPacket
{
    // 这里只保存业务载荷，不含帧头和 CRC。
    // buildWorkflowPacket() 负责生成载荷，sendPacket() 负责封帧，职责不要混用，
    // 否则容易发生帧头重复或 CRC 计算范围不一致。
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

    // 打包为：[0xA6][payload...][CRC低字节][CRC高字节]。
    // CRC 覆盖帧头和全部 payload，不覆盖末尾两个 CRC 占位字节。
    // payload 内部已经包含版本、队伍、消息类型、会话、序号和数据长度。
    bool sendPacket(const VisionTxPacket &packet, int maxRetries = 3);

private:
    bool configurePort();
    std::string findAvailablePort() const;
    bool tryReconnect();

    SerialConfig config_;
    int fd_ = -1;
};

#endif // VIRTUAL_SERIAL_H
