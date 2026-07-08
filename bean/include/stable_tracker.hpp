/**
 * stable_tracker.hpp —— 稳定跟踪状态机
 * 连续 N 帧检测到同一物体才确认，避免误触发。
 * 发送后进入冷却期，冷却期满自动解锁，允许再次触发同一目标。
 */

#ifndef STABLE_TRACKER_HPP_
#define STABLE_TRACKER_HPP_

#include <string>
#include <vector>
#include <optional>
#include "detection.hpp"
#include "config.hpp"

class StableTracker {
public:
    explicit StableTracker(int threshold = 10, int cooldown = 150)
        : threshold_(threshold), cooldown_frames_(cooldown) {}

    /**
     * @brief 更新跟踪状态
     * @param detections 当前帧检测结果
     * @param classNames 类别名称表
     * @return 如果稳定确认，返回目标名称；否则返回 nullopt
     */
    std::optional<std::string> update(
        const std::vector<Detection>& detections,
        const std::vector<std::string>& classNames)
    {
        const Detection* best = bestDetection(detections);
        if (!best) {
            reset();
            return std::nullopt;
        }

        std::string name = classNames[best->class_id];

        // 冷却期：发送后沉默 N 帧，期满自动解锁
        if (name == last_sent_) {
            cooldown_counter_++;
            if (cooldown_counter_ >= cooldown_frames_) {
                last_sent_.clear();
                cooldown_counter_ = 0;
                // 冷却期满，从头开始计数
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

    /// 完全重置
    void reset()
    {
        stable_counter_ = 0;
        stable_name_.clear();
        // 注意：last_sent_ 和 cooldown 不在这里清除，
        // 只在冷却期满后自动清除，避免短暂丢帧钻空子
    }

    // ---- 调试信息 ----
    const std::string& stableName()    const { return stable_name_; }
    int stableCounter()                const { return stable_counter_; }
    int threshold()                    const { return threshold_; }
    const std::string& lastSent()      const { return last_sent_; }
    int cooldownCounter()              const { return cooldown_counter_; }
    int cooldownFrames()               const { return cooldown_frames_; }

private:
    static const Detection* bestDetection(const std::vector<Detection>& dets)
    {
        if (dets.empty()) return nullptr;
        const Detection* best = &dets[0];
        for (const auto& d : dets)
            if (d.confidence > best->confidence) best = &d;
        return best;
    }

    std::string stable_name_;
    int stable_counter_ = 0;
    std::string last_sent_;
    int threshold_;
    int cooldown_frames_;
    int cooldown_counter_ = 0;
};

#endif  // STABLE_TRACKER_HPP_
