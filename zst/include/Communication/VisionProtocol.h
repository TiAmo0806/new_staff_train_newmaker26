#ifndef VISION_PROTOCOL_H
#define VISION_PROTOCOL_H

#include "Communication/VirtualSerial.h"
#include "ImgProcessing/FieldState.h"
#include <cstdint>
#include <vector>

// 队伍模式只在视觉电脑内部选择状态机，不作为线路字段发送给电控。
enum class TeamMode : uint8_t
{
    TeamA = 1,  // 先识别3个豆子，再识别5个数字，最后发送6字节位置结果
    TeamB = 2   // 中心豆子、数字数组、剩余中心豆子分阶段发送
};

// 视觉发给电控的业务消息类型。当前使用无ACK双向通信，不发送SEQ。
enum class VisionMessageType : uint8_t
{
    // A组最终结果固定6字节：
    // [黄豆位置, 绿豆位置, 白芸豆位置, 数字1位置, 数字2位置, 数字3位置]。
    // 前3字节表示三种豆子从左到右位于第几个位置；后3字节表示对应数字箱的位置。
    TeamAResult = 0x10,
    BeanCode = 0x20,       // B组：DATA为1个中心豆子编码
    DigitLayout = 0x21     // B组：DATA为place1~place5上的数字
};

// 创建业务包，VirtualSerial随后添加0xA6帧头和CRC16。
// 完整线路帧：[A6][CMD][DATA...][CRC_L][CRC_H]。
// 每种CMD的数据长度固定，因此无需额外发送LENGTH字段。
VisionTxPacket buildWorkflowPacket(VisionMessageType type,
                                   const std::vector<uint8_t> &data);

// 根据CMD返回固定DATA长度；未知CMD返回0。
uint8_t visionMessageDataLength(VisionMessageType type);

// 以下函数只用于终端日志，不把字符串发送到电控。
const char *teamModeToString(TeamMode mode);
const char *visionMessageTypeToString(VisionMessageType type);

#endif // VISION_PROTOCOL_H
