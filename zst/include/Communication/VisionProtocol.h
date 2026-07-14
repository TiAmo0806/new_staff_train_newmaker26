#ifndef VISION_PROTOCOL_H
#define VISION_PROTOCOL_H

#include "Communication/VirtualSerial.h"
#include "ImgProcessing/FieldState.h"
#include <cstdint>
#include <vector>

// 队伍模式只在视觉电脑内部决定运行哪套状态机，不再作为线路字段发送给电控。
enum class TeamMode : uint8_t
{
    TeamA = 1,  // 先发送5个数字，再发送3个豆子
    TeamB = 2   // 中心豆子、数字数组、剩余中心豆子分阶段发送，豆子顺序不固定
};

// 视觉发给电控的业务消息类型；每条结果都需要电控返回ACK。
enum class VisionMessageType : uint8_t
{
    DigitsComplete = 0x10, // 队伍A：五个数字位置已全部稳定，DATA为5字节数字
    BeansComplete = 0x11,  // 队伍A：三个豆子位置已全部稳定，DATA为3字节豆子类型
    BeanCode = 0x20,       // 队伍B：1字节豆子码，1黄豆、2绿豆、3白芸豆
    DigitLayout = 0x21     // 队伍B：5字节，place1~place5上各自识别到的数字
};

// 最简可靠协议：本函数生成独立的CMD、SEQ、DATA，VirtualSerial负责添加0xA6和CRC16。
// CMD就是VisionMessageType；每种CMD的数据长度固定，因此仍然不发送LENGTH。
//
// 完整线路帧：
//   A6 CMD SEQ DATA... CRC_L CRC_H
//
// frame[0]=A6，frame[1]=CMD，frame[2]=SEQ，frame[3...]为固定长度DATA，
// 最后两字节为CRC低/高。CRC覆盖A6、CMD、SEQ和全部DATA。
// SEQ范围为1~255；视觉超时重发时必须保持相同SEQ，电控用它识别重复结果。
//
// 固定长度：10->5字节，11->3字节，20->1字节，21->5字节。
VisionTxPacket buildWorkflowPacket(VisionMessageType type,
                                   uint8_t sequence,
                                   const std::vector<uint8_t> &data);

// 根据CMD返回固定DATA长度；未知CMD返回0。
uint8_t visionMessageDataLength(VisionMessageType type);

// 将队伍模式枚举转为字符串，仅用于日志输出
const char *teamModeToString(TeamMode mode);
// 将消息类型枚举转为字符串，仅用于终端调试
const char *visionMessageTypeToString(VisionMessageType type);

#endif // VISION_PROTOCOL_H
