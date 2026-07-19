#include "Communication/VisionProtocol.h"
#include <stdexcept>

namespace
{
uint8_t expectedDataLength(VisionMessageType type)
{
    switch (type)
    {
    case VisionMessageType::TeamABeanPositions: return 3;
    case VisionMessageType::TeamADigitPositions: return 3;
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
    packet.command = static_cast<uint8_t>(type); // CMD：A组0x10/0x11，B组0x20/0x21
    packet.data = data;                          // 固定长度业务数据
    return packet;
}

uint8_t visionMessageDataLength(VisionMessageType type)
{
    return expectedDataLength(type);
}

const char *teamModeToString(TeamMode mode)
{
    return mode == TeamMode::TeamB ? "team_b" : "team_a";
}

const char *visionMessageTypeToString(VisionMessageType type)
{
    switch (type)
    {
    case VisionMessageType::TeamABeanPositions: return "team_a_bean_positions";
    case VisionMessageType::TeamADigitPositions: return "team_a_digit_positions";
    case VisionMessageType::BeanCode: return "bean_code";
    case VisionMessageType::DigitLayout: return "digit_layout";
    default: return "unknown";
    }
}
