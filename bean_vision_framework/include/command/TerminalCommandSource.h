#pragma once

#include "command/CommandSource.h"

/**
 * @brief 从终端读取调试命令。
 */
class TerminalCommandSource : public CommandSource {
public:
    bool next(std::string& line) override;
};
