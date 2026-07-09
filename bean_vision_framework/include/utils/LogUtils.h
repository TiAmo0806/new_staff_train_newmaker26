#pragma once

#include "core/DetectionTypes.h"
#include "core/TaskTypes.h"
#include "core/VisionResult.h"

#include <vector>

class LogUtils {
public:
    /**
     * @brief 打印原始检测结果。
     * @param detections 检测器输出的 Detection 列表。
     */
    static void printDetections(const std::vector<Detection>& detections);

    /**
     * @brief 打印 ROI 解析后的视觉结果。
     * @param result ROI 解析后的 VisionResult。
     */
    static void printVisionResult(const VisionResult& result);

    /**
     * @brief 打印任务生成结果。
     * @param result TaskGenerator 输出的 TaskResult。
     */
    static void printTaskResult(const TaskResult& result);
};
