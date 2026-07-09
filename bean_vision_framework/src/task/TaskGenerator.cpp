#include "task/TaskGenerator.h"

#include <algorithm>
#include <array>
#include <map>

namespace {

/**
 * @brief 将豆子类别名转换成协议中的数字编码。
 * @param class_name 豆子类别名。
 * @return 0 表示黄豆，1 表示绿豆，2 表示白芸豆，255 表示未知类别。
 */
uint8_t beanCode(const std::string& class_name) {
    // 协议里用数字表示豆子类别，方便电控端解析。
    if (class_name == "soybean") {
        return 0;
    }
    if (class_name == "mung_bean") {
        return 1;
    }
    if (class_name == "white_kidney_bean") {
        return 2;
    }
    return 255;
}

/**
 * @brief 根据豆子类别查询需要匹配的数字箱类别。
 * @param bean_name 豆子类别名。
 * @return 对应的数字类别名；未知豆子返回空字符串。
 */
std::string requiredDigit(const std::string& bean_name) {
    // 比赛规则：不同豆子对应不同数字箱。
    if (bean_name == "soybean") {
        return "digit_1";
    }
    if (bean_name == "mung_bean") {
        return "digit_2";
    }
    if (bean_name == "white_kidney_bean") {
        return "digit_3";
    }
    return "";
}

}  // namespace

/**
 * @brief 根据视觉识别结果生成搬运任务。
 * @param vision ROI 解析后的视觉结果。
 * @return 任务生成结果。
 */
TaskResult TaskGenerator::generate(const VisionResult& vision) {
    TaskResult result;

    // 把结构体字段放进数组，方便用同一段逻辑处理 P1/P2/P3 和 L4-L8。
    //把分散的P1/P2/P3和L4-L8放进数组，方便统一处理。
    const std::array<PositionResult, 3> pickups = {vision.p1, vision.p2, vision.p3};
    const std::array<PositionResult, 5> places = {vision.l4, vision.l5, vision.l6, vision.l7, vision.l8};
    const std::map<std::string, uint8_t> pickup_ids = {{"P1", 1}, {"P2", 2}, {"P3", 3}};
    const std::map<std::string, uint8_t> place_ids = {{"L4", 4}, {"L5", 5}, {"L6", 6}, {"L7", 7}, {"L8", 8}};

    for (const auto& pickup : pickups) {
        if (!pickup.valid) {
            // 某个取货位置没有识别到豆子，任务无法完整生成。
            result.reason = pickup.position_id + "_missing";
            continue;
        }

        const std::string digit = requiredDigit(pickup.class_name);
        if (digit.empty()) {
            // 识别到了东西，但不是已知豆子类别。
            result.reason = pickup.position_id + "_unknown_bean";
            return result;
        }

        // 在所有放置位置里寻找对应数字箱。
        auto place_it = std::find_if(places.begin(), places.end(), [&](const PositionResult& place) {
            return place.valid && place.class_name == digit;
        });
        //返回一个指向vector<PositionResult> places中第一个匹配的迭代器，如果没有找到就返回places.end()。

        if (place_it == places.end()) {
            // 找不到目标数字箱，例如绿豆需要 digit_2，但 L4-L8 都没有 digit_2。
            result.reason = digit + "_missing";
            return result;
        }

        // 生成一条可以直接打包给电控的搬运任务。
        Task task;
        task.from = pickup_ids.at(pickup.position_id);
        task.to = place_ids.at(place_it->position_id);
        task.bean = beanCode(pickup.class_name);
        result.tasks.push_back(task);
    }

    result.success = !result.tasks.empty();
    result.reason = result.success ? "ok" : "no_task_generated";
    return result;
}
