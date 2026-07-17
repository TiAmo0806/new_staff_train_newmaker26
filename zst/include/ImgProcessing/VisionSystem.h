#ifndef VISION_SYSTEM_H
#define VISION_SYSTEM_H

#include "ImgProcessing/TaskPlanner.h"
#include "ImgProcessing/YoloOpenVinoDetector.h"
#include <opencv2/core.hpp>
#include <vector>

struct VisionSystemConfig
{
    YoloConfig yolo;            // YOLO OpenVINO推理参数：模型、设备、缓存、尺寸和阈值
    PlannerConfig planner;      // 任务规划器参数：稳定帧数
};

struct VisionFrameResult
{
    std::vector<Detection> detections;  // 当前帧所有检测框（豆子+数字箱）
    VisionDecision decision;            // 当前帧的视觉决策（目标箱+偏差）
    cv::Mat debugImage;                 // 画好框和文字的调试图，可直接 imshow
};

class VisionSystem
{
public:
    // 初始化OpenVINO YOLO和任务规划器。
    explicit VisionSystem(const VisionSystemConfig &config);

    // 完成一帧完整视觉处理。
    VisionFrameResult process(const cv::Mat &frame);

private:
    // 绘制检测框和最终决策。
    void drawResult(cv::Mat &image, const std::vector<Detection> &detections,
                    const VisionDecision &decision) const;

    YoloOpenVinoDetector yolo_;                // OpenVINO直接加载ONNX的YOLO推理器
    TaskPlanner planner_;                      // 比赛规则决策器
};

#endif // VISION_SYSTEM_H
