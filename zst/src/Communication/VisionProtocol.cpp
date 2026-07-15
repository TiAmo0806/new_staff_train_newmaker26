#include "Communication/VisionProtocol.h"
#include <stdexcept>

namespace
{
uint8_t expectedDataLength(VisionMessageType type)
{
    switch (type)
    {
    case VisionMessageType::TeamAResult: return 6;
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
    packet.command = static_cast<uint8_t>(type); // CMD：A组0x10，B组0x20/0x21
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
    case VisionMessageType::TeamAResult: return "team_a_result";
    case VisionMessageType::BeanCode: return "bean_code";
    case VisionMessageType::DigitLayout: return "digit_layout";
    default: return "unknown";
    }
}
