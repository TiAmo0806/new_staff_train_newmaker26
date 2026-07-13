#include "Communication/VisionProtocol.h"
#include <stdexcept>

namespace
{
uint8_t expectedDataLength(VisionMessageType type)
{
    switch (type)
    {
    case VisionMessageType::DigitsComplete: return 5;
    case VisionMessageType::BeansComplete: return 3;
    case VisionMessageType::BeanCode: return 1;
    case VisionMessageType::DigitLayout: return 5;
    default: return 0;
    }
}
}

VisionTxPacket buildWorkflowPacket(VisionMessageType type,
                                   const std::vector<uint8_t> &data)
{
    const uint8_t expected = expectedDataLength(type);
    if (expected == 0 || data.size() != expected)
        throw std::invalid_argument("invalid DATA length for vision command");

    VisionTxPacket packet;
    packet.payload.reserve(1 + data.size());
    packet.payload.push_back(static_cast<uint8_t>(type));   // [0] CMD
    packet.payload.insert(packet.payload.end(), data.begin(), data.end()); // [1..] DATA
    return packet;
}

uint8_t visionMessageDataLength(VisionMessageType type)
{
    return expectedDataLength(type);
}

const char *teamModeToString(TeamMode mode)
{
    // 仅用于日志显示，不会把字符串发送到C板。
    return mode == TeamMode::TeamB ? "team_b" : "team_a";  // 默认返回 team_a
}

const char *visionMessageTypeToString(VisionMessageType type)
{
    // 仅用于终端日志，线路上发送的是VisionMessageType对应的uint8_t数值。
    switch (type)
    {
    case VisionMessageType::DigitsComplete: return "digits_complete";     // 队伍A数字阶段完成
    case VisionMessageType::BeansComplete: return "beans_complete";       // 队伍A豆子阶段完成
    case VisionMessageType::BeanCode: return "bean_code";                 // 队伍B单个豆子类型码
    case VisionMessageType::DigitLayout: return "digit_layout";           // 队伍B五个箱位上的数字
    default: return "unknown";                                            // 未知消息类型
    }
}
