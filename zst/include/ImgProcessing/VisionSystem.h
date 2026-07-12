#ifndef VISION_SYSTEM_H
#define VISION_SYSTEM_H

#include "ImgProcessing/BeanSvmClassifier.h"
#include "ImgProcessing/TaskPlanner.h"
#include "ImgProcessing/YoloOrtDetector.h"
#include <memory>
#include <opencv2/core.hpp>
#include <string>
#include <vector>

struct VisionSystemConfig
{
    YoloConfig yolo;
    std::string svmPath;
    bool useSvm = true;
    PlannerConfig planner;
};

struct VisionFrameResult
{
    std::vector<Detection> detections;
    VisionDecision decision;
    cv::Mat debugImage;
};

class VisionSystem
{
public:
    // 初始化 YOLO、SVM、任务规划器。
    explicit VisionSystem(const VisionSystemConfig &config);

    // 完成一帧完整视觉处理。
    VisionFrameResult process(const cv::Mat &frame);

private:
    // 用 SVM 复核 YOLO 检出的豆子类别。
    void refineBeansBySvm(const cv::Mat &frame, std::vector<Detection> &detections) const;

    // 绘制检测框和最终决策。
    void drawResult(cv::Mat &image, const std::vector<Detection> &detections,
                    const VisionDecision &decision) const;

    VisionSystemConfig config_;
    YoloOrtDetector yolo_;
    std::unique_ptr<BeanSvmClassifier> svm_;
    TaskPlanner planner_;
};

#endif // VISION_SYSTEM_H
