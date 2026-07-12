#include "Communication/VisionProtocol.h"
#include <stdexcept>

namespace
{
// 修改协议字段含义或布局时应递增版本，让C板能拒绝不兼容的旧/新帧。
constexpr uint8_t PROTOCOL_VERSION = 1;     // 当前协议版本号
}

VisionTxPacket buildWorkflowPacket(TeamMode team,
                                   VisionMessageType type,
                                   uint8_t session,
                                   uint8_t sequence,
                                   const std::vector<uint8_t> &data)
{
    // DATA_LENGTH只有1字节，所以单条消息最多携带255字节业务数据。
    if (data.size() > 255)
        throw std::length_error("workflow packet data exceeds 255 bytes");  // 超过单字节容量

    VisionTxPacket packet;
    // 这里不放0xA6和CRC；它们由VirtualSerial统一添加。
    // payload的6个固定字段依次为版本、队伍、类型、会话、序号、DATA长度。
    packet.payload.reserve(6 + data.size());                // 预分配内存，避免多次扩容
    packet.payload.push_back(PROTOCOL_VERSION);             // [0] 协议版本
    packet.payload.push_back(static_cast<uint8_t>(team));   // [1] 队伍编号
    packet.payload.push_back(static_cast<uint8_t>(type));   // [2] 消息类型
    packet.payload.push_back(session);                      // [3] 会话号
    packet.payload.push_back(sequence);                     // [4] 消息序号
    packet.payload.push_back(static_cast<uint8_t>(data.size())); // [5] DATA 长度
    packet.payload.insert(packet.payload.end(), data.begin(), data.end()); // [6..] 业务数据
    return packet;
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
    case VisionMessageType::FinalResult: return "final_result";           // 队伍A最终完整结果
    default: return "unknown";                                            // 未知消息类型
    }
}
