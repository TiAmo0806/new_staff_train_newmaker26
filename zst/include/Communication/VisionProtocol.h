#ifndef VISION_PROTOCOL_H
#define VISION_PROTOCOL_H

#include "Communication/VirtualSerial.h"
#include "ImgProcessing/FieldState.h"
#include <cstdint>
#include <vector>

// 两个队伍不会同时上场，因此共用同一个物理串口和同一套基础协议。
// 区别由 TEAM 和 TYPE 字段表达，而不是复制两套 CRC/串口代码。
enum class TeamMode : uint8_t
{
    TeamA = 1,  // 先数字后豆子，一次性发送完整结果
    TeamB = 2   // 黄豆、数字数组、绿豆、白芸豆分阶段发送
};

// 视觉单向发给电控的业务消息类型。
enum class VisionMessageType : uint8_t
{
    DigitsComplete = 0x10, // 队伍A：五个数字位置已全部稳定，DATA为5字节数字
    BeansComplete = 0x11,  // 队伍A：三个豆子位置已全部稳定，DATA为3字节豆子类型
    BeanCode = 0x20,       // 队伍B：1字节豆子码，1黄豆、2绿豆、3白芸豆
    DigitLayout = 0x21,    // 队伍B：5字节，place1~place5上各自识别到的数字
    FinalResult = 0x30     // 队伍A：完整豆子、数字和对应位置，DATA为11字节
};

// VirtualSerial 在外层继续添加 0xA6 和 CRC16。
// 本函数生成的 payload 格式：
//   VERSION TEAM TYPE SESSION SEQUENCE DATA_LENGTH DATA...
//
// 完整线路帧：
//   A6 VERSION TEAM TYPE SESSION SEQ LEN DATA... CRC_L CRC_H
//
// 完整帧各偏移（DATA长度为N）：
//   frame[0]       = 0xA6 固定帧头
//   frame[1]       = VERSION，目前固定1
//   frame[2]       = TEAM，1=TeamA，2=TeamB
//   frame[3]       = TYPE，决定DATA应该按哪种格式解析
//   frame[4]       = SESSION，切换队伍/新任务时用于区分旧数据
//   frame[5]       = SEQUENCE，本会话业务消息序号
//   frame[6]       = N，只统计DATA，不包含帧头、协议字段和CRC
//   frame[7..6+N]  = DATA
//   frame[7+N]     = CRC低字节
//   frame[8+N]     = CRC高字节
//
// 因此完整帧总长度 = 9 + N。CRC覆盖从A6到最后一个DATA字节。
VisionTxPacket buildWorkflowPacket(TeamMode team,
                                   VisionMessageType type,
                                   uint8_t session,
                                   uint8_t sequence,
                                   const std::vector<uint8_t> &data);

// 将队伍模式枚举转为字符串，仅用于日志输出
const char *teamModeToString(TeamMode mode);
// 将消息类型枚举转为字符串，仅用于终端调试
const char *visionMessageTypeToString(VisionMessageType type);

#endif // VISION_PROTOCOL_H
