#include "command/TerminalCommandSource.h"

#include <iostream>

/**
 * @brief 从标准输入读取一行终端命令。
 * @param line 输出命令文本。
 * @return 成功读取返回 true；输入结束返回 false。
 */
bool TerminalCommandSource::next(std::string& line) {
    std::cout << "[COMMAND][TERMINAL] > " << std::flush;

    if (!std::getline(std::cin, line)) {
        std::cout << "\n[COMMAND][TERMINAL] input closed\n";
        return false;
    }

    if (!line.empty()) {
        std::cout << "[COMMAND][TERMINAL] received=" << line << "\n";
    }

    return true;
}
