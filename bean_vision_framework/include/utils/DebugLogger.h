#pragma once

#include "core/AppConfig.h"
#include "core/DetectionTypes.h"
#include "core/VisionResult.h"

#include <opencv2/core.hpp>

#include <string>
#include <vector>

class DebugLogger {
public:
    /**
     * @brief 保存命令模式下的一帧调试图片。
     * @param event_name 事件名，例如 arrive_bean 或 arrive_digit。
     * @param image_path 原始图片路径，用于生成输出文件名。
     * @param frame 原始图像。
     * @param detections YOLO 检测结果。
     * @param result ROI 解析结果。
     * @param config 总配置，读取 ROI 和 debug 开关。
     * @param force_save 为 true 时忽略 yaml 开关，强制保存 raw/result 图片。
     */
    static void saveCommandImages(const std::string& event_name,
                                  const std::string& image_path,
                                  const cv::Mat& frame,
                                  const std::vector<Detection>& detections,
                                  const VisionResult& result,
                                  const AppConfig& config,
                                  bool force_save);
};
