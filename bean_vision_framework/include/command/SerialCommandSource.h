#pragma once

#include "command/CommandSource.h"
#include "communication/Protocol.h"
#include "communication/SerialPort.h"

/**
 * @brief 从真实串口读取电控命令的预留入口。
 *
 * 当前阶段还没有定义 C 板下发命令包解析，因此先保留接口。
 */
class SerialCommandSource : public CommandSource {
public:
    explicit SerialCommandSource(SerialPort& serial);

    bool next(std::string& line) override;

private:
    Protocol protocol_;
    SerialPort& serial_;
};
