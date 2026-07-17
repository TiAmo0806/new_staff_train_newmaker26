/**
 * stable_tracker.cpp —— 稳定跟踪状态机实现
 *
 * 策略：
 *   连续 N 帧检测到同一目标 → 确认发送。
 *   发送后记录 last_sent_，同一目标不再触发；
 *   仅当切换到不同目标并稳定确认后，才发送新指令。
 */

#include "stable_tracker.hpp"

std::optional<std::string> StableTracker::update(
    const std::vector<Detection>& detections)
{
    // 选面积最大的（面积大 ≈ 距离近，当前最需要处理的豆子）
    if (detections.empty()) {
        stable_counter_ = 0;
        stable_name_.clear();
        return std::nullopt;
    }

    const Detection* best = &detections[0];
    for (const auto& d : detections)
        if (d.bbox.area() > best->bbox.area()) best = &d;

    std::string name = CLASS_NAMES[best->class_id];

    // 与上次发送的目标相同 → 不重复发送
    if (name == last_sent_) {
        stable_counter_ = 0;
        stable_name_.clear();
        return std::nullopt;
    }

    // 持续一致则计数
    if (name == stable_name_) {
        stable_counter_++;
    } else {
        // 切换到新目标 → 重新计数
        stable_name_ = name;
        stable_counter_ = 1;
    }

    // 达到稳定阈值 → 确认发送，并记录
    if (stable_counter_ >= threshold_) {
        last_sent_ = name;
        stable_counter_ = 0;
        stable_name_.clear();
        return name;
    }

    return std::nullopt;
}
