#pragma once

#include "command/CommandSource.h"
#include "communication/Protocol.h"
#include "communication/SerialPort.h"

/**
 * @brief 从真实串口读取电控命令。
 *
 * 该类负责把协议包转换为主流程可消费的命令字符串，
 * 自身不推进状态机，也不负责业务处理。
 */
class SerialCommandSource : public CommandSource {
public:
    explicit SerialCommandSource(SerialPort& serial);

    bool next(std::string& line) override;

private:
    Protocol protocol_;
    SerialPort& serial_;
};
