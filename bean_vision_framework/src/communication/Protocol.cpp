#include "communication/Protocol.h"

#include "communication/ByteConverter.h"
#include "communication/CRC16.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

bool isKnownCommand(uint8_t cmd) {
    switch (static_cast<ProtocolCommand>(cmd)) {
    case ProtocolCommand::Vision:
    case ProtocolCommand::FinalTask:
    case ProtocolCommand::Error:
    case ProtocolCommand::BeanBind:
    case ProtocolCommand::Pong:
    case ProtocolCommand::ArriveBean:
    case ProtocolCommand::ArriveDigit:
    case ProtocolCommand::Reset:
    case ProtocolCommand::Ping:
    case ProtocolCommand::Ack:
        return true;
    }
    return false;
}

std::string hexByte(uint8_t value) {
    std::ostringstream oss;
    oss << "0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(value);
    return oss.str();
}

}  // namespace

const char* Protocol::commandName(uint8_t cmd) {
    switch (static_cast<ProtocolCommand>(cmd)) {
    case ProtocolCommand::Vision:
        return "VISION";
    case ProtocolCommand::FinalTask:
        return "FINAL_TASK";
    case ProtocolCommand::Error:
        return "ERROR";
    case ProtocolCommand::BeanBind:
        return "BEAN_BIND";
    case ProtocolCommand::Pong:
        return "PONG";
    case ProtocolCommand::ArriveBean:
        return "ARRIVE_BEAN";
    case ProtocolCommand::ArriveDigit:
        return "ARRIVE_DIGIT";
    case ProtocolCommand::Reset:
        return "RESET";
    case ProtocolCommand::Ping:
        return "PING";
    case ProtocolCommand::Ack:
        return "ACK";
    }
    return "UNKNOWN";
}

/**
 * @brief 将视觉结果打包成协议帧。
 * @param result ROI 解析后的视觉结果。
 * @return 完整协议帧字节数组。
 */
std::vector<uint8_t> Protocol::makeVisionPacket(const VisionResult& result) {
    // 视觉结果包：把每个固定位置的类别 id 放进 payload。
    // 这个包目前没有在 main 里发送，但保留接口方便后续调试或发给电控。
    std::vector<uint8_t> payload;
    payload.push_back(result.success ? 1 : 0);
    payload.push_back(result.p1.valid ? static_cast<uint8_t>(result.p1.class_id) : 255);
    payload.push_back(result.p2.valid ? static_cast<uint8_t>(result.p2.class_id) : 255);
    payload.push_back(result.p3.valid ? static_cast<uint8_t>(result.p3.class_id) : 255);
    payload.push_back(result.l4.valid ? static_cast<uint8_t>(result.l4.class_id) : 255);
    payload.push_back(result.l5.valid ? static_cast<uint8_t>(result.l5.class_id) : 255);
    payload.push_back(result.l6.valid ? static_cast<uint8_t>(result.l6.class_id) : 255);
    payload.push_back(result.l7.valid ? static_cast<uint8_t>(result.l7.class_id) : 255);
    payload.push_back(result.l8.valid ? static_cast<uint8_t>(result.l8.class_id) : 255);
    return makePacket(static_cast<uint8_t>(ProtocolCommand::Vision), payload);
}

namespace {

uint8_t pickupCode(const std::string& pickup_id) {
    if (pickup_id == "P1") {
        return 1;
    }
    if (pickup_id == "P2") {
        return 2;
    }
    if (pickup_id == "P3") {
        return 3;
    }
    return 0;
}

uint8_t bindBeanCode(const std::string& bean_name) {
    if (bean_name == "soybean") {
        return 0;
    }
    if (bean_name == "mung_bean") {
        return 1;
    }
    if (bean_name == "white_kidney_bean") {
        return 2;
    }
    return 255;
}

uint8_t digitCode(const std::string& digit_name) {
    if (digit_name == "digit_1") {
        return 1;
    }
    if (digit_name == "digit_2") {
        return 2;
    }
    if (digit_name == "digit_3") {
        return 3;
    }
    if (digit_name == "digit_4") {
        return 4;
    }
    if (digit_name == "digit_5") {
        return 5;
    }
    return 0;
}

}  // namespace

std::vector<uint8_t> Protocol::makeBeanBindPacket(const std::vector<BeanBind>& binds) {
    std::vector<uint8_t> payload;
    payload.push_back(static_cast<uint8_t>(std::min<size_t>(binds.size(), 3)));

    for (size_t i = 0; i < 3; ++i) {
        if (i < binds.size() && binds[i].valid) {
            payload.push_back(pickupCode(binds[i].pickup_id));
            payload.push_back(bindBeanCode(binds[i].bean_class));
            payload.push_back(digitCode(binds[i].target_digit));
        } else {
            payload.push_back(0);
            payload.push_back(255);
            payload.push_back(0);
        }
    }

    return makePacket(static_cast<uint8_t>(ProtocolCommand::BeanBind), payload);
}

/**
 * @brief 将任务结果打包成协议帧。
 * @param result 任务生成模块输出的任务结果。
 * @return 完整协议帧字节数组。
 */
std::vector<uint8_t> Protocol::makeTaskPacket(const TaskResult& result) {
    // 任务结果包：这是当前主流程真正发送的包。
    // payload[0] = 状态，payload[1] = 任务数量，后面每 3 个字节表示一条任务。
    std::vector<uint8_t> payload;
    payload.push_back(result.success ? 1 : 0);
    payload.push_back(static_cast<uint8_t>(std::min<size_t>(result.tasks.size(), 3)));

    for (size_t i = 0; i < 3; ++i) {
        if (i < result.tasks.size()) {
            // 一条任务：from, to, bean。
            payload.push_back(result.tasks[i].from);
            payload.push_back(result.tasks[i].to);
            payload.push_back(result.tasks[i].bean);
        } else {
            // 不足 3 条任务时补 0，让包长度固定，电控端更好解析。
            payload.push_back(0);
            payload.push_back(0);
            payload.push_back(0);
        }
    }

    return makePacket(static_cast<uint8_t>(ProtocolCommand::FinalTask), payload);
}

std::vector<uint8_t> Protocol::makePongPacket() {
    return makePacket(static_cast<uint8_t>(ProtocolCommand::Pong), {});
}

std::vector<uint8_t> Protocol::makeAckPacket(uint8_t acked_cmd, uint8_t acked_seq) {
    return makePacket(static_cast<uint8_t>(ProtocolCommand::Ack), {acked_cmd, acked_seq});
}

/**
 * @brief 生成错误状态协议帧。
 * @param error_code 错误码。
 * @return 完整协议帧字节数组。
 */
std::vector<uint8_t> Protocol::makeErrorPacket(uint8_t error_code) {
    // 错误包接口，后续可以把异常状态单独发给电控。
    return makePacket(static_cast<uint8_t>(ProtocolCommand::Error), {error_code});
}

/**
 * @brief 解析统一协议帧。
 * @param packet 完整协议帧。
 * @return 解析结果。
 */
ParsedPacket Protocol::parsePacket(const std::vector<uint8_t>& packet) const {
    ParsedPacket parsed;
    if (packet.size() < 6) {
        parsed.reason = "packet_too_short";
        return parsed;
    }
    if (packet[0] != 0xA5) {
        parsed.reason = "bad_header";
        return parsed;
    }

    parsed.cmd = packet[1];
    parsed.length = packet[2];
    parsed.seq = packet[3];

    const size_t expected_size = 4U + parsed.length + 2U;
    if (packet.size() != expected_size) {
        parsed.reason = "bad_length";
        std::cout << "[WARN] drop frame: invalid length\n";
        return parsed;
    }

    const uint16_t received_crc =
        static_cast<uint16_t>(packet[packet.size() - 2]) |
        (static_cast<uint16_t>(packet[packet.size() - 1]) << 8U);
    const std::vector<uint8_t> crc_input(packet.begin(), packet.end() - 2);
    const uint16_t calculated_crc = CRC16::calculate(crc_input);
    if (received_crc != calculated_crc) {
        parsed.reason = "bad_crc";
        std::cout << "[WARN] drop frame: crc mismatch\n";
        return parsed;
    }

    parsed.payload.assign(packet.begin() + 4, packet.end() - 2);
    parsed.valid = true;
    parsed.reason = "ok";
    if (!isKnownCommand(parsed.cmd)) {
        std::cout << "[WARN] unknown command: " << hexByte(parsed.cmd) << "\n";
    }
    return parsed;
}

/**
 * @brief 根据命令字和 payload 生成统一格式的数据包。
 * @param cmd 命令字。
 * @param payload 业务数据区。
 * @return 带 header、length、seq 和 CRC 的完整协议帧。
 */
std::vector<uint8_t> Protocol::makePacket(uint8_t cmd, const std::vector<uint8_t>& payload) {
    // 统一封包格式：
    // 0xA5 cmd length seq payload... crc_l crc_h
    std::vector<uint8_t> packet;
    packet.push_back(0xA5);
    packet.push_back(cmd);
    packet.push_back(static_cast<uint8_t>(payload.size()));
    packet.push_back(++seq_);       // seq 每发一包自增，方便接收端判断是否丢包。
    packet.insert(packet.end(), payload.begin(), payload.end());

    const uint16_t crc = CRC16::calculate(packet);
    // CRC 低字节在前，高字节在后。
    ByteConverter::appendUint16LE(packet, crc);
    return packet;
}
