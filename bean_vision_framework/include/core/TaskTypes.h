#pragma once

#include <cstdint>
#include <string>
#include <vector>

// 一条搬运任务：从哪个取货点拿哪种豆子，送到哪个放置点。
struct Task {
    uint8_t from = 0;               // 取货位置：P1=1，P2=2，P3=3。
    uint8_t to = 0;                 // 放置位置：L4=4，L5=5，L6=6，L7=7，L8=8。
    uint8_t bean = 0;               // 豆子类别：0 黄豆，1 绿豆，2 白芸豆。
};

// TaskResult 是任务生成模块的输出，会继续交给 Protocol 打包。
struct TaskResult {
    bool success = false;           // 是否成功生成完整任务。
    std::string reason;             // 失败原因，例如 digit_2_missing。
    std::vector<Task> tasks;        // 搬运任务列表。
};
