#include "ImgProcessing/VisionSystem.h"
#include <algorithm>
#include <iostream>
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
    : config_(config), yolo_(config.yolo), planner_(config.planner)
{
    // SVM 是可选模块。
    // 如果没有 bean_svm.yml，YOLO 仍然能独立工作。
    if (config_.useSvm && !config_.svmPath.empty())
    {
        svm_ = std::make_unique<BeanSvmClassifier>();
        if (!svm_->load(config_.svmPath)) svm_.reset();
    }
}

VisionFrameResult VisionSystem::process(const cv::Mat &frame)
{
    VisionFrameResult result;

    // 主识别：YOLO 找豆子和数字箱。
    // YOLO 是主模型，负责定位所有目标。
    result.detections = yolo_.infer(frame);             // ONNX Runtime CPU 推理

    // 辅助识别：SVM 只复核豆子类别。
    // 如果 SVM 和 YOLO 判断不同，这里会把豆子 label 改成 SVM 结果。
    refineBeansBySvm(frame, result.detections);          // SVM 复核（可选）

    // 规则规划：豆子类别 -> 目标数字箱。
    // 这里不做运动控制，只给出视觉侧”应该去哪”的结果。
    result.decision = planner_.update(result.detections, frame.size());  // 任务规划
    result.debugImage = frame.clone();                   // 克隆一帧用于绘制
    drawResult(result.debugImage, result.detections, result.decision);   // 画框和文字
    return result;
}

void VisionSystem::refineBeansBySvm(const cv::Mat &frame, std::vector<Detection> &detections) const
{
    if (!svm_ || !svm_->isReady()) return;              // SVM 未加载或未就绪
    cv::Rect imageRect(0, 0, frame.cols, frame.rows);   // 图像边界，用于裁剪超出范围的框
    for (auto &d : detections)
    {
        // 只处理豆子框，数字箱不交给 SVM。
        if (d.kind != TargetKind::Bean) continue;       // 跳过非豆子检测
        cv::Rect safeBox = d.box & imageRect;           // 裁剪到图像范围内
        if (safeBox.width < 5 || safeBox.height < 5) continue; // ROI 太小，跳过
        BeanType svmBean = svm_->predict(frame(safeBox)); // SVM 对裁剪区域分类
        if (svmBean != BeanType::Unknown)
        {
            d.bean = svmBean;                           // 覆盖 YOLO 的豆子类别
            d.label = beanToString(svmBean) + "_svm";   // 标签加 _svm 后缀，方便调试
        }
    }
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
