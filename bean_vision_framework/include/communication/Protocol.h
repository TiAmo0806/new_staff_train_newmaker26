#pragma once

#include "core/TaskTypes.h"
#include "core/VisionResult.h"
#include "task/VisionMemory.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct ParsedPacket {
    bool valid = false;                 // CRC 和长度都正确时为 true。
    uint8_t cmd = 0;                    // 命令字。
    uint8_t length = 0;                 // payload 长度。
    uint8_t seq = 0;                    // 包序号。
    std::vector<uint8_t> payload;       // 业务数据区。
    std::string reason;                 // 无效时的原因说明。
};

enum class ProtocolCommand : uint8_t {
    Vision = 0x01,
    FinalTask = 0x02,
    Error = 0x03,
    BeanBind = 0x04,
    Pong = 0x05,
    ArriveBean = 0x10,
    ArriveDigit = 0x11,
    Reset = 0x12,
    Ping = 0x13,
    Ack = 0x14
};

class Protocol {
public:
    // 当前协议版本中最长合法 payload 是 FINAL_TASK 的 11 字节。
    static constexpr size_t kMaxPayloadLength = 11;

    static const char* commandName(uint8_t cmd);

    /**
     * @brief 将视觉结果打包成协议帧。
     * @param result ROI 解析后的视觉结果。
     * @return 完整协议帧字节数组。
     *
     * 当前主流程主要发送任务包，此接口保留给调试或后续扩展使用。
     */
    std::vector<uint8_t> makeVisionPacket(const VisionResult& result);

    std::vector<uint8_t> makeBeanBindPacket(const std::vector<BeanBind>& binds);

    /**
     * @brief 将任务结果打包成协议帧。
     * @param result 任务生成模块输出的任务结果。
     * @return 完整协议帧字节数组。
     */
    std::vector<uint8_t> makeTaskPacket(const TaskResult& result);

    std::vector<uint8_t> makePongPacket();

    std::vector<uint8_t> makeAckPacket(uint8_t acked_cmd, uint8_t acked_seq);

    /**
     * @brief 生成错误状态协议帧。
     * @param error_code 错误码，由业务层自行约定。
     * @return 完整协议帧字节数组。
     */
    std::vector<uint8_t> makeErrorPacket(uint8_t error_code);

    /**
     * @brief 解析统一协议帧。
     * @param packet 完整协议帧：A5 cmd length seq payload crc_l crc_h。
     * @return 解析结果，valid=false 时 reason 描述失败原因。
     */
    ParsedPacket parsePacket(const std::vector<uint8_t>& packet) const;

private:
    /**
     * @brief 根据命令字和 payload 生成统一格式的数据包。
     * @param cmd 命令字，例如 0x01 视觉包、0x02 任务包、0x03 错误包。
     * @param payload 业务数据区。
     * @return 带 header、length、seq 和 CRC 的完整协议帧。
     */
    std::vector<uint8_t> makePacket(uint8_t cmd, const std::vector<uint8_t>& payload);

    uint8_t seq_ = 0;
};
