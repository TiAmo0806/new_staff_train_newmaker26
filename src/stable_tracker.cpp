/**
 * stable_tracker.cpp —— 稳定跟踪状态机实现
 */

#include "stable_tracker.hpp"

const Detection* StableTracker::bestDetection(const std::vector<Detection>& dets)
{
    if (dets.empty()) return nullptr;
    const Detection* best = &dets[0];
    for (const auto& d : dets)
        if (d.confidence > best->confidence) best = &d;
    return best;
}

std::optional<std::string> StableTracker::update(
    const std::vector<Detection>& detections)
{
    const Detection* best = bestDetection(detections);
    if (!best) {
        reset();
        return std::nullopt;
    }

    std::string name = CLASS_NAMES[best->class_id];

    // 冷却期：发送后沉默 N 帧，期满自动解锁
    if (name == last_sent_) {
        cooldown_counter_++;
        if (cooldown_counter_ >= cooldown_frames_) {
            last_sent_.clear();
            cooldown_counter_ = 0;
            stable_name_ = name;
            stable_counter_ = 1;
        }
        return std::nullopt;
    }

    // 持续一致则计数
    if (name == stable_name_) {
        stable_counter_++;
    } else {
        stable_name_ = name;
        stable_counter_ = 1;
    }

    // 达到阈值 → 确认，进入冷却
    if (stable_counter_ >= threshold_) {
        last_sent_ = name;
        cooldown_counter_ = 0;
        stable_counter_ = 0;
        stable_name_.clear();
        return name;
    }

    return std::nullopt;
}
