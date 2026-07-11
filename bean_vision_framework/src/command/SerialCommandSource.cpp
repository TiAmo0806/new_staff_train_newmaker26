#include "command/SerialCommandSource.h"

#include "communication/Protocol.h"

#include <iostream>

/**
 * @brief 构造串口命令来源。
 * @param serial 已打开的串口模块。
 */
SerialCommandSource::SerialCommandSource(SerialPort& serial) : serial_(serial) {}

/**
 * @brief 从串口读取下一条电控命令。
 * @param line 输出转换后的命令文本。
 * @return 成功读取并转换出一条命令时返回 true；串口读取失败时返回 false。
 */
bool SerialCommandSource::next(std::string& line) {
    line.clear();

    while (true) {
        std::vector<uint8_t> packet;
        if (!serial_.readAvailable(packet)) {
            return false;
        }
        if (packet.empty()) {
            continue;
        }

        const ParsedPacket parsed = protocol_.parsePacket(packet);
        if (!parsed.valid) {
            std::cout << "[WARN] invalid serial packet: " << parsed.reason << "\n";
            continue;
        }

        switch (static_cast<ProtocolCommand>(parsed.cmd)) {
        case ProtocolCommand::ArriveBean:
            serial_.writeAck(parsed.cmd, parsed.seq);
            // Bean 流程当前已迁移到 RecognitionRunner，状态机只要求命令格式保留 image_path 占位。
            line = "arrive_bean __serial__";
            return true;
        case ProtocolCommand::ArriveDigit:
            serial_.writeAck(parsed.cmd, parsed.seq);
            // Digit 流程仍走旧的图片路径逻辑，先保留占位参数格式以维持主循环接线。
            line = "arrive_digit __serial__";
            return true;
        case ProtocolCommand::Reset:
            serial_.writeAck(parsed.cmd, parsed.seq);
            line = "reset";
            return true;
        case ProtocolCommand::Ping:
            serial_.writePong();
            break;
        case ProtocolCommand::Ack:
            break;
        default:
            std::cout << "[WARN] unsupported serial command: " << Protocol::commandName(parsed.cmd) << "\n";
            break;
        }
    }
}
