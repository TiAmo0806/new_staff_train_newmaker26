#ifndef VIRTUAL_SERIAL_H
#define VIRTUAL_SERIAL_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct SerialConfig
{
    // Linux 下常见串口名：/dev/ttyACM0 或 /dev/ttyUSB0。
    std::string port = "/dev/ttyACM0";  // 串口设备路径

    // 期望波特率。注意：当前 configurePort() 只实现 115200，
    // 修改 YAML 中的数值不会自动改变 termios 常量，扩展其他波特率时需增加映射。
    int baudrate = 115200;              // 波特率，默认 115200

    // true：模拟模式，不打开 /dev 设备，也不真正发送，只按配置打印完整帧。
    // false：真实模式，程序会按 port 打开串口并执行 write()。
    bool simulated = true;              // 是否使用模拟串口（无需硬件）

    // true 表示打印十六进制发送帧。
    bool txLog = true;                  // 是否在终端打印发送帧的十六进制内容

    // true 表示收到合法的相机控制帧后打印完整十六进制帧和camera_state。
    // 关闭后仍会打印CRC错误、非法状态等异常信息，方便发现协议不一致。
    bool rxLog = true;                  // 是否打印电控发给视觉的接收帧

    // 视觉结果发出后等待电控ACK的时间。超过该时间仍未收到匹配ACK时，
    // main会使用完全相同的CMD、SEQ和DATA重新发送，避免电控漏收后双方流程错位。
    int ackTimeoutMs = 300;             // ACK超时重发间隔，单位毫秒
};

struct VisionTxPacket
{
    // 业务包把CMD、SEQ、DATA分开保存，sendPacket()再统一封装线路帧。
    // SEQ用于把ACK与某一次识别结果严格对应；重发时三个字段必须保持不变。
    uint8_t command = 0;            // 业务命令：0x10/0x11/0x20/0x21
    uint8_t sequence = 0;           // 结果序号：1~255，0保留不用
    std::vector<uint8_t> data;      // 每种CMD对应的固定长度业务数据
};

// 电控确认视觉结果时发送的固定命令码。
// 第二字节使用0x80，因此不会和原有camera_state=0/1混淆。
constexpr uint8_t RESULT_ACK_COMMAND = 0x80;

// ACK状态码。0表示电控已经通过CRC和业务检查并接受结果；
// 非0表示电控拒绝该结果，视觉保留待确认包并在超时后使用同一SEQ重发。
enum class ResultAckStatus : uint8_t
{
    Ok = 0x00,            // 已正确接收并接受
    InvalidData = 0x01,   // DATA长度或内容不合法
    WrongStage = 0x02     // 电控当前阶段不接受该CMD
};

// 串口接收层一次可能解析出相机控制帧或结果ACK帧，使用统一事件交给main处理，
// 避免两个接收函数争抢同一个Linux串口缓存。
enum class SerialRxEventType : uint8_t
{
    CameraState,          // [5A][0/1][CRC_L][CRC_H]
    ResultAck             // [5A][80][SEQ][原CMD][STATUS][CRC_L][CRC_H]
};

struct SerialRxEvent
{
    SerialRxEventType type = SerialRxEventType::CameraState;
    uint8_t cameraState = 0;       // type=CameraState时有效，只可能为0或1
    uint8_t sequence = 0;          // type=ResultAck时有效
    uint8_t originalCommand = 0;   // ACK确认的原视觉CMD
    uint8_t status = 0;            // ResultAckStatus对应的uint8_t值
};

// 电控发给视觉的相机控制帧固定为4字节，不使用可变长度结构体：
//   [0] 0x5A         接收方向帧头
//   [1] cameraState  0=关闭相机，1=打开相机
//   [2] crcLow       CRC16低字节
//   [3] crcHigh      CRC16高字节
// 全部字段都是uint8_t，因此不存在结构体补齐或浮点字节序问题。
struct CameraControlPacket
{
    uint8_t header = 0x5A;          // 电控->视觉的固定帧头
    uint8_t cameraState = 1;        // 相机命令：0关闭，1打开
    uint8_t crcLow = 0;             // CRC16低8位，先发送
    uint8_t crcHigh = 0;            // CRC16高8位，后发送
};

static_assert(sizeof(CameraControlPacket) == 4,
              "CameraControlPacket在线路上必须严格等于4字节");

class VirtualSerial
{
public:
    // 两个方向使用不同帧头，接收器即使在字节流中错位也能重新找到下一帧。
    static constexpr uint8_t FRAME_HEADER = 0xA6;             // 视觉->电控
    static constexpr uint8_t RX_FRAME_HEADER = 0x5A;          // 电控->视觉
    static constexpr size_t CAMERA_CONTROL_FRAME_SIZE = 4;    // 5A+状态+CRC16
    static constexpr size_t RESULT_ACK_FRAME_SIZE = 7;        // 5A+80+SEQ+原CMD+状态+CRC16

    explicit VirtualSerial(const SerialConfig &config);
    ~VirtualSerial();

    bool openPort();
    void closePort();
    bool isOpen() const;

    // 打包为：[0xA6][CMD][SEQ][DATA...][CRC低字节][CRC高字节]。
    // CRC覆盖帧头、CMD、SEQ和全部DATA，不覆盖末尾两个CRC占位字节。
    // 同一个待确认结果重发时，CMD、SEQ、DATA必须完全相同。
    bool sendPacket(const VisionTxPacket &packet, int maxRetries = 3);

    // 非阻塞读取电控发送的相机控制或结果ACK。
    // 返回true：成功解析出一个完整事件；返回false：当前没有完整合法帧。
    // Linux一次read可能得到半帧、一帧或多帧，未解析完的字节会留到下次继续处理。
    bool receiveEvent(SerialRxEvent &event);

private:
    bool configurePort();               // 配置 termios：115200-8-N-1，无流控，原始模式
    std::string findAvailablePort() const;  // 扫描 /dev 下可用的 ttyACM/ttyUSB 设备
    bool tryReconnect();                // 原端口失效后重新扫描并打开新端口

    SerialConfig config_;               // 串口配置的只读副本
    int fd_ = -1;                       // Linux 文件描述符，-1 表示未打开或模拟模式

    // Linux read()可能一次只返回半帧，也可能一次返回多帧，必须使用持续缓存按帧解析。
    std::vector<uint8_t> receiveBuffer_; // 尚未解析完成的电控->视觉字节流

    // 串口掉线时每秒最多尝试一次重连，避免主循环每帧扫描/dev并刷屏。
    std::chrono::steady_clock::time_point nextReceiveReconnectAttempt_{};
};

#endif // VIRTUAL_SERIAL_H
