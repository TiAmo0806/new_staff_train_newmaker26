#include "/home/zst/zst/include/Communication/VisionProtocol.h"
#include <stdexcept>

namespace
{
// 修改协议字段含义或布局时应递增版本，让C板能拒绝不兼容的旧/新帧。
constexpr uint8_t PROTOCOL_VERSION = 1;
}

VisionTxPacket buildWorkflowPacket(TeamMode team,
                                   VisionMessageType type,
                                   uint8_t session,
                                   uint8_t sequence,
                                   const std::vector<uint8_t> &data)
{
    // DATA_LENGTH只有1字节，所以单条消息最多携带255字节业务数据。
    if (data.size() > 255)
        throw std::length_error("workflow packet data exceeds 255 bytes");

    VisionTxPacket packet;
    // 这里不放0xA6和CRC；它们由VirtualSerial统一添加。
    // payload的6个固定字段依次为版本、队伍、类型、会话、序号、DATA长度。
    packet.payload.reserve(6 + data.size());
    packet.payload.push_back(PROTOCOL_VERSION);
    packet.payload.push_back(static_cast<uint8_t>(team));
    packet.payload.push_back(static_cast<uint8_t>(type));
    packet.payload.push_back(session);
    packet.payload.push_back(sequence);
    packet.payload.push_back(static_cast<uint8_t>(data.size()));
    packet.payload.insert(packet.payload.end(), data.begin(), data.end());
    return packet;
}

const char *teamModeToString(TeamMode mode)
{
    // 仅用于日志显示，不会把字符串发送到C板。
    return mode == TeamMode::TeamB ? "team_b" : "team_a";
}

const char *visionMessageTypeToString(VisionMessageType type)
{
    // 仅用于终端日志，线路上发送的是VisionMessageType对应的uint8_t数值。
    switch (type)
    {
    case VisionMessageType::DigitsComplete: return "digits_complete";
    case VisionMessageType::BeansComplete: return "beans_complete";
    case VisionMessageType::BeanDetected: return "bean_detected";
    case VisionMessageType::BeanDigitMatch: return "bean_digit_match";
    case VisionMessageType::FinalResult: return "final_result";
    default: return "unknown";
    }
}
