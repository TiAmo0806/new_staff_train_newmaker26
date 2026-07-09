#pragma once

#include "core/AppConfig.h"
#include "core/DetectionTypes.h"
#include "core/VisionResult.h"

#include <opencv2/core.hpp>

#include <string>
#include <vector>

class RoiParser {
public:
    /**
     * @brief 构造 ROI 解析器。
     * @param config ROI 配置，包含 P1/P2/P3 和 L4-L8 的矩形区域。
     */
    explicit RoiParser(const RoiConfig& config);

    /**
     * @brief 将原始检测框解析成固定位置结果。
     * @param detections 检测器输出的原始检测框列表。
     * @return ROI 解析后的 VisionResult。
     */
    VisionResult parse(const std::vector<Detection>& detections);

private:
    /**
     * @brief 判断一个点是否落在 ROI 内。
     * @param point 待判断的图像像素点。
     * @param roi ROI 矩形区域。
     * @return 点在 ROI 内返回 true，否则返回 false。
     */
    bool pointInRoi(const cv::Point& point, const cv::Rect& roi) const;

    /**
     * @brief 从候选检测框中选择置信度最高的目标。
     * @param candidates 落入同一个 ROI 的候选检测框。
     * @param position_id 固定位置编号，例如 P1 或 L5。
     * @param roi 当前固定位置对应的 ROI。
     * @return 该位置的解析结果；没有候选时 valid=false。
     */
    PositionResult chooseBest(const std::vector<Detection>& candidates,
                              const std::string& position_id,
                              const cv::Rect& roi) const;

    /**
     * @brief 判断检测结果是否属于豆子类别。
     * @param detection 待判断的检测结果。
     * @return 是 soybean、mung_bean 或 white_kidney_bean 时返回 true。
     */
    bool isBean(const Detection& detection) const;

    /**
     * @brief 判断检测结果是否属于数字类别。
     * @param detection 待判断的检测结果。
     * @return 是 digit_1 到 digit_5 时返回 true。
     */
    bool isDigit(const Detection& detection) const;

    RoiConfig config_;
};
