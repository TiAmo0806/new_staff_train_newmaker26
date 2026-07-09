#include "parser/RoiParser.h"

#include <algorithm>

/**
 * @brief 构造 ROI 解析器。
 * @param config ROI 配置。
 */
RoiParser::RoiParser(const RoiConfig& config) : config_(config) {}

/**
 * @brief 将检测框解析为固定位置结果。
 * @param detections 原始检测框列表。
 * @return 固定位置识别结果。
 */
VisionResult RoiParser::parse(const std::vector<Detection>& detections) {
    VisionResult result;

    // 解析一个固定位置：
    // 1. 找出中心点落在 ROI 内的检测框。
    // 2. 根据 want_bean 过滤豆子或数字。
    // 3. 如果候选不止一个，选择置信度最高的。
    auto parseOne = [&](const std::string& id, const cv::Rect& roi, bool want_bean) {
        std::vector<Detection> candidates;
        for (const auto& detection : detections) {
            if (pointInRoi(detection.center(), roi) &&
                ((want_bean && isBean(detection)) || (!want_bean && isDigit(detection)))) {
                candidates.push_back(detection);
            }
        }
        //上一步返回了一个vector<Detection> candidates，里面是所有落在ROI内的检测框。
        return chooseBest(candidates, id, roi);
    };

    // 取货区只接受豆子类别。
    result.p1 = parseOne("P1", config_.pickup_rois.at("P1"), true);
    result.p2 = parseOne("P2", config_.pickup_rois.at("P2"), true);
    result.p3 = parseOne("P3", config_.pickup_rois.at("P3"), true);

    // 放置区只接受数字类别。
    result.l4 = parseOne("L4", config_.place_rois.at("L4"), false);
    result.l5 = parseOne("L5", config_.place_rois.at("L5"), false);
    result.l6 = parseOne("L6", config_.place_rois.at("L6"), false);
    result.l7 = parseOne("L7", config_.place_rois.at("L7"), false);
    result.l8 = parseOne("L8", config_.place_rois.at("L8"), false);

    // 这里先只要求 P1/P2/P3 都识别到，真正缺哪个数字由 TaskGenerator 给出更具体原因。
    result.success = result.p1.valid && result.p2.valid && result.p3.valid;
    result.reason = result.success ? "ok" : "pickup_position_missing";
    return result;
}

/**
 * @brief 判断点是否在 ROI 内。
 * @param point 待判断点。
 * @param roi ROI 矩形。
 * @return 在 ROI 内返回 true，否则返回 false。
 */
bool RoiParser::pointInRoi(const cv::Point& point, const cv::Rect& roi) const {
    // OpenCV 的 Rect::contains 会判断点是否在矩形内部。
    return roi.contains(point);
}

/**
 * @brief 从候选框中选择置信度最高的结果。
 * @param candidates 候选检测框列表。
 * @param position_id 固定位置编号。
 * @param roi 固定位置对应的 ROI。
 * @return 该固定位置的解析结果。
 */
PositionResult RoiParser::chooseBest(const std::vector<Detection>& candidates,
                                     const std::string& position_id,
                                     const cv::Rect& roi) const {
    PositionResult result;
    result.position_id = position_id;

    if (candidates.empty()) {
        // 没有候选时保持 valid=false。
        return result;
    }

    // 同一个 ROI 内可能出现多个框，这里选择置信度最高的一个。
    const auto best = std::max_element(candidates.begin(), candidates.end(),
        [](const Detection& a, const Detection& b) {
            return a.confidence < b.confidence;
        });

    // 保存被选中的目标信息，后续 TaskGenerator 只需要看 PositionResult。
    result.valid = true;
    result.class_id = best->class_id;
    result.class_name = best->class_name;
    result.confidence = best->confidence;
    result.box = best->box;
    result.center_px = best->center();
    result.offset_px = result.center_px - cv::Point(roi.x + roi.width / 2, roi.y + roi.height / 2);
    return result;
}

/**
 * @brief 判断检测结果是否为豆子类别。
 * @param detection 检测结果。
 * @return 是豆子类别返回 true。
 */
bool RoiParser::isBean(const Detection& detection) const {
    // 取货区允许的类别。
    return detection.class_name == "soybean" ||
           detection.class_name == "mung_bean" ||
           detection.class_name == "white_kidney_bean";
}

/**
 * @brief 判断检测结果是否为数字类别。
 * @param detection 检测结果。
 * @return 是数字类别返回 true。
 */
bool RoiParser::isDigit(const Detection& detection) const {
    // 放置区允许的类别。
    return detection.class_name == "digit_1" ||
           detection.class_name == "digit_2" ||
           detection.class_name == "digit_3" ||
           detection.class_name == "digit_4" ||
           detection.class_name == "digit_5";
}
