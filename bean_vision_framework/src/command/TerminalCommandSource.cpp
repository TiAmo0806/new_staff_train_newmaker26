#include "command/TerminalCommandSource.h"

#include <iostream>

/**
 * @brief 从标准输入读取一行终端命令。
 * @param line 输出命令文本。
 * @return 成功读取返回 true；输入结束返回 false。
 */
bool TerminalCommandSource::next(std::string& line) {
    return static_cast<bool>(std::getline(std::cin, line));
}
