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
        const std::vector<Detection>& detections);

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
    static const Detection* bestDetection(const std::vector<Detection>& dets);

    std::string stable_name_;
    int stable_counter_ = 0;
    std::string last_sent_;
    int threshold_;
    int cooldown_frames_;
    int cooldown_counter_ = 0;
};

#endif  // STABLE_TRACKER_HPP_
