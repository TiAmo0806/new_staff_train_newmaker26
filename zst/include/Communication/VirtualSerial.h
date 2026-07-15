#ifndef VIRTUAL_SERIAL_H
#define VIRTUAL_SERIAL_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct SerialConfig
{
    std::string port = "/dev/ttyACM0"; // Linux串口设备路径
    int baudrate = 115200;             // 当前实现固定使用115200-8-N-1
    bool simulated = true;             // true只打印帧，不访问真实串口
    bool txLog = true;                 // 是否打印视觉发送帧
    bool rxLog = true;                 // 是否打印电控相机控制帧
};

struct VisionTxPacket
{
    uint8_t command = 0;           // 业务命令：A组0x10，B组0x20/0x21
    std::vector<uint8_t> data;     // 每种CMD对应的固定长度业务数据
};

// 电控发给视觉的相机控制帧固定为4字节：
// [0x5A][camera_state][CRC_L][CRC_H]，camera_state只能是0或1。
struct CameraControlPacket
{
    uint8_t header = 0x5A;
    uint8_t cameraState = 1;
    uint8_t crcLow = 0;
    uint8_t crcHigh = 0;
};

static_assert(sizeof(CameraControlPacket) == 4,
              "CameraControlPacket在线路上必须严格等于4字节");

class VirtualSerial
{
public:
    static constexpr uint8_t FRAME_HEADER = 0xA6;          // 视觉->电控结果帧头
    static constexpr uint8_t RX_FRAME_HEADER = 0x5A;       // 电控->视觉控制帧头
    static constexpr size_t CAMERA_CONTROL_FRAME_SIZE = 4; // 5A+状态+CRC16

    explicit VirtualSerial(const SerialConfig &config);
    ~VirtualSerial();

    bool openPort();
    void closePort();
    bool isOpen() const;

    // 发送：[0xA6][CMD][DATA...][CRC_L][CRC_H]。
    // CRC覆盖帧头、CMD和全部DATA，不包含末尾两个CRC字节。
    bool sendPacket(const VisionTxPacket &packet, int maxRetries = 3);

    // 非阻塞接收电控相机控制帧。可处理半帧、粘包、噪声和串口重连。
    // 返回true表示解析到一个CRC正确的camera_state。
    bool receiveCameraState(uint8_t &cameraState);

private:
    bool configurePort();
    std::string findAvailablePort() const;
    bool tryReconnect();

    SerialConfig config_;
    int fd_ = -1;
    std::vector<uint8_t> receiveBuffer_;
    std::chrono::steady_clock::time_point nextReceiveReconnectAttempt_{};
};

#endif // VIRTUAL_SERIAL_H
