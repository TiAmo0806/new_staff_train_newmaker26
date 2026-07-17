#pragma once

#include "core/VisionResult.h"

#include <string>
#include <vector>

struct BeanBind {
    std::string pickup_id;      // P1/P2/P3.
    std::string bean_class;     // soybean/mung_bean/white_kidney_bean.
    std::string target_digit;   // digit_1/digit_2/digit_3.
    float confidence = 0.0f;
    bool valid = false;
};

/**
 * @brief 跨帧缓存视觉识别结果。
 *
 * VisionMemory 用来保存不同阶段看到的 ROI 结果。
 * 豆子扫描阶段只缓存 P1/P2/P3，数字扫描阶段只缓存 L4-L8。
 */
class VisionMemory {
public:
    /**
     * @brief 用当前帧结果更新豆子区缓存。
     * @param current 当前帧 ROI 解析后的视觉结果。
     */
    void updateBeans(const VisionResult& current);

    /**
     * @brief 用当前帧结果更新数字区缓存。
     * @param current 当前帧 ROI 解析后的视觉结果。
     */
    void updateDigits(const VisionResult& current);

    /**
     * @brief 获取豆子与目标数字的绑定关系。
     * @return 豆子绑定关系列表。
     */
    const std::vector<BeanBind>& beanBinds() const;

    /**
     * @brief 判断豆子区是否已经缓存到有效结果。
     * @return P1/P2/P3 全部有效时返回 true。
     */
    bool beansReady() const;

    /**
     * @brief 判断数字区是否已经缓存到有效结果。
     * @return 至少一个 L4-L8 有效时返回 true。
     */
    bool digitsReady() const;

    /**
     * @brief 判断缓存信息是否足够尝试生成任务。
     * @return 豆子区和数字区都已有有效结果时返回 true。
     */
    bool readyForTask() const;

    /**
     * @brief 返回合并后的视觉结果。
     * @return 可继续交给 TaskGenerator 的 VisionResult。
     */
    VisionResult mergedResult() const;

    /**
     * @brief 清空缓存，准备下一轮任务。
     */
    void clear();

private:
    VisionResult cached_beans_;
    VisionResult cached_digits_;
    std::vector<BeanBind> bean_binds_;
};
