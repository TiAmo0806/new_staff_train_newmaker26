#include "utils/LogUtils.h"

#include <iostream>

namespace {

/**
 * @brief 打印单个固定位置的识别结果。
 * @param result 单个固定位置的识别结果。
 */
void printPosition(const PositionResult& result) {
    std::cout << "  " << result.position_id << ": ";
    if (!result.valid) {
        std::cout << "missing\n";
        return;
    }
    std::cout << result.class_name
              << " conf=" << result.confidence
              << " center=(" << result.center_px.x << "," << result.center_px.y << ")"
              << " offset=(" << result.offset_px.x << "," << result.offset_px.y << ")\n";
}

}  // namespace

/**
 * @brief 打印原始检测结果。
 * @param detections 检测器输出的 Detection 列表。
 */
void LogUtils::printDetections(const std::vector<Detection>& detections) {
    // 打印检测器的原始输出，主要用于检查模型输出是否正常。
    std::cout << "detections: " << detections.size() << "\n";
    for (const auto& detection : detections) {
        std::cout << "  class=" << detection.class_name
                  << " id=" << detection.class_id
                  << " conf=" << detection.confidence
                  << " box=[" << detection.box.x << "," << detection.box.y
                  << "," << detection.box.width << "," << detection.box.height << "]\n";
    }
}

/**
 * @brief 打印 ROI 解析后的视觉结果。
 * @param result ROI 解析后的 VisionResult。
 */
void LogUtils::printVisionResult(const VisionResult& result) {
    // 打印 ROI 解析后的固定位置结果。
    std::cout << "parsed VisionResult: " << (result.success ? "success" : "fail")
              << " reason=" << result.reason << "\n";
    printPosition(result.p1);
    printPosition(result.p2);
    printPosition(result.p3);
    printPosition(result.l4);
    printPosition(result.l5);
    printPosition(result.l6);
    printPosition(result.l7);
    printPosition(result.l8);
}

/**
 * @brief 打印任务生成结果。
 * @param result TaskGenerator 输出的 TaskResult。
 */
void LogUtils::printTaskResult(const TaskResult& result) {
    // 打印最终生成的搬运任务。
    std::cout << "generated TaskResult: " << (result.success ? "success" : "fail")
              << " reason=" << result.reason << "\n";
    for (const auto& task : result.tasks) {
        std::cout << "  P" << static_cast<int>(task.from)
                  << " -> L" << static_cast<int>(task.to)
                  << " bean=" << static_cast<int>(task.bean) << "\n";
    }
}
