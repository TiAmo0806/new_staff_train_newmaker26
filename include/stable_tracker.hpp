/**
 * stable_tracker.hpp —— 稳定跟踪状态机
 * 连续 N 帧检测到同一物体才确认，避免误触发。
 * 发送后仅当目标发生变化时才允许再次发送，同一目标不重复发送。
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
    explicit StableTracker(int threshold = 10)
        : threshold_(threshold) {}

    /**
     * @brief 更新跟踪状态
     * @param detections 当前帧检测结果
     * @return 如果稳定确认且目标与上次不同，返回目标名称；否则返回 nullopt
     *
     * 行为：
     *   - 连续 threshold_ 帧检测到同一目标 → 确认发送
     *   - 发送后，同一目标不再触发（直到切换到其他目标后才重置）
     *   - 目标丢失或切换 → 重新计数
     */
    std::optional<std::string> update(
        const std::vector<Detection>& detections);

    /// 完全重置（包括已发送记录）
    void reset()
    {
        stable_counter_ = 0;
        stable_name_.clear();
        last_sent_.clear();
    }

    // ---- 调试信息 ----
    const std::string& stableName()    const { return stable_name_; }
    int stableCounter()                const { return stable_counter_; }
    int threshold()                    const { return threshold_; }
    const std::string& lastSent()      const { return last_sent_; }

private:
    std::string stable_name_;
    int stable_counter_ = 0;
    std::string last_sent_;       // 上一次已发送的目标（变化前不重复发送）
    int threshold_;
};

#endif  // STABLE_TRACKER_HPP_
