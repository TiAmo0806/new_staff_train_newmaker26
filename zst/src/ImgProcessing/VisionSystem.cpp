#include "ImgProcessing/VisionSystem.h"
#include <algorithm>
#include <iostream>
#include <opencv2/imgproc.hpp>

namespace
{
cv::Scalar colorFor(const Detection &d)
{
    if (d.kind == TargetKind::Bean) return cv::Scalar(0, 220, 255);
    if (d.kind == TargetKind::DigitBox) return cv::Scalar(80, 255, 80);
    return cv::Scalar(255, 255, 255);
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
    result.detections = yolo_.infer(frame);

    // 辅助识别：SVM 只复核豆子类别。
    // 如果 SVM 和 YOLO 判断不同，这里会把豆子 label 改成 SVM 结果。
    refineBeansBySvm(frame, result.detections);

    // 规则规划：豆子类别 -> 目标数字箱。
    // 这里不做运动控制，只给出视觉侧“应该去哪”的结果。
    result.decision = planner_.update(result.detections, frame.size());
    result.debugImage = frame.clone();
    drawResult(result.debugImage, result.detections, result.decision);
    return result;
}

void VisionSystem::refineBeansBySvm(const cv::Mat &frame, std::vector<Detection> &detections) const
{
    if (!svm_ || !svm_->isReady()) return;
    cv::Rect imageRect(0, 0, frame.cols, frame.rows);
    for (auto &d : detections)
    {
        // 只处理豆子框，数字箱不交给 SVM。
        if (d.kind != TargetKind::Bean) continue;
        cv::Rect safeBox = d.box & imageRect;
        if (safeBox.width < 5 || safeBox.height < 5) continue;
        BeanType svmBean = svm_->predict(frame(safeBox));
        if (svmBean != BeanType::Unknown)
        {
            d.bean = svmBean;
            d.label = beanToString(svmBean) + "_svm";
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
        cv::rectangle(image, d.box, colorFor(d), 2);
        std::string text = d.label + " " + cv::format("%.2f", d.score);
        cv::putText(image, text, cv::Point(d.box.x, std::max(20, d.box.y - 5)),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, colorFor(d), 2);
    }

    std::string line = decision.valid
        ? ("OK bean=" + beanToString(decision.bean) + " target=" + std::to_string(decision.targetDigit))
        : ("WAIT " + decision.reason);
    cv::putText(image, line, cv::Point(20, 40), cv::FONT_HERSHEY_SIMPLEX,
                0.9, decision.valid ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255), 2);
    if (decision.valid)
    {
        cv::circle(image, decision.targetCenter, 6, cv::Scalar(0, 0, 255), -1);
        cv::line(image, cv::Point(image.cols / 2, 0), cv::Point(image.cols / 2, image.rows),
                 cv::Scalar(255, 100, 0), 1);
    }
}
