#pragma once

#include <string>

/**
 * @brief 统一的命令来源接口。
 *
 * 主流程只关心“下一条命令文本是什么”，不关心命令来自终端还是串口。
 */
class CommandSource {
public:
    virtual ~CommandSource() = default;

    /**
     * @brief 读取下一条命令。
     * @param line 输出命令文本，例如 arrive_bean D:/xxx.png。
     * @return 成功读取返回 true；需要退出循环时返回 false。
     */
    virtual bool next(std::string& line) = 0;
};
