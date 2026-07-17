#include "ImgProcessing/VisionSystem.h"
#include <algorithm>
#include <opencv2/imgproc.hpp>

namespace
{
// 根据检测类型返回不同颜色：豆子用黄色，数字箱用绿色，未知用白色
cv::Scalar colorFor(const Detection &d)
{
    if (d.kind == TargetKind::Bean) return cv::Scalar(0, 220, 255);       // BGR: 黄色
    if (d.kind == TargetKind::DigitBox) return cv::Scalar(80, 255, 80);   // BGR: 绿色
    return cv::Scalar(255, 255, 255);                                      // BGR: 白色
}
}

VisionSystem::VisionSystem(const VisionSystemConfig &config)
    : yolo_(config.yolo), planner_(config.planner)
{
}

VisionFrameResult VisionSystem::process(const cv::Mat &frame)
{
    VisionFrameResult result;

    // 主识别：YOLO 找豆子和数字箱。
    // YOLO 是主模型，负责定位所有目标。
    result.detections = yolo_.infer(frame);             // OpenVINO CPU/AUTO设备推理

    // 规则规划：豆子类别 -> 目标数字箱。
    // 这里不做运动控制，只给出视觉侧”应该去哪”的结果。
    result.decision = planner_.update(result.detections, frame.size());  // 任务规划
    result.debugImage = frame.clone();                   // 克隆一帧用于绘制
    drawResult(result.debugImage, result.detections, result.decision);   // 画框和文字
    return result;
}

void VisionSystem::drawResult(cv::Mat &image, const std::vector<Detection> &detections,
                              const VisionDecision &decision) const
{
    // 调试图用于现场看模型是否识别对。
    // 黄色框表示豆子，绿色框表示数字箱。
    for (const auto &d : detections)
    {
        cv::rectangle(image, d.box, colorFor(d), 2);                    // 画检测框
        std::string text = d.label + " " + cv::format("%.2f", d.score); // 标签+置信度
        cv::putText(image, text, cv::Point(d.box.x, std::max(20, d.box.y - 5)),  // 框上方
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, colorFor(d), 2);
    }

    // 顶部状态栏：OK（绿色）或 WAIT（红色），显示豆子类别、目标数字或等待原因
    std::string line = decision.valid
        ? ("OK bean=" + beanToString(decision.bean) + " target=" + std::to_string(decision.targetDigit))
        : ("WAIT " + decision.reason);
    cv::putText(image, line, cv::Point(20, 40), cv::FONT_HERSHEY_SIMPLEX,
                0.9, decision.valid ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255), 2);  // 绿/红色
    if (decision.valid)
    {
        cv::circle(image, decision.targetCenter, 6, cv::Scalar(0, 0, 255), -1);  // 目标点红圆
        // 垂直中线，辅助观察目标偏差
        cv::line(image, cv::Point(image.cols / 2, 0), cv::Point(image.cols / 2, image.rows),
                 cv::Scalar(255, 100, 0), 1);
    }
}
