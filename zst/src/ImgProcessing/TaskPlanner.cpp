#include "ImgProcessing/TaskPlanner.h"
#include <algorithm>

TaskPlanner::TaskPlanner(const PlannerConfig &config) : config_(config) {}

Detection TaskPlanner::chooseBestBean(const std::vector<Detection> &detections) const
{
    Detection best;                                     // 默认构造，score=0
    for (const auto &d : detections)
        if (d.kind == TargetKind::Bean && d.score > best.score)
         best = d; // 选置信度最高的豆子
    return best;
}

Detection TaskPlanner::chooseTargetBox(const std::vector<Detection> &detections, int digit) const
{
    Detection best;
    for (const auto &d : detections)
        if (d.kind == TargetKind::DigitBox && d.digit == digit && d.score > best.score) best = d; // 选指定数字中置信度最高的
    return best;
}

VisionDecision TaskPlanner::update(const std::vector<Detection> &detections, const cv::Size &imageSize)
{
    VisionDecision decision;

    // 第一步：先确定当前看到的豆子类别。
    // 目前策略很简单：选置信度最高的豆子框。
    Detection bean = chooseBestBean(detections);        // 选最优豆子
    if (bean.bean == BeanType::Unknown)
    {
        // OpenCV 默认 putText 不支持中文，调试画面用 ASCII 状态码显示。
        decision.reason = "no_bean";                    // 未识别到任何豆子
        stableCount_ = 0;                               // 重置稳定计数
        return decision;
    }

    // 第二步：按规则找到该豆子应该去的数字箱。
    //   黄豆必须放入数字 1 货箱；
    //   绿豆必须放入数字 2 货箱；
    //   白芸豆必须放入数字 3 货箱；
    //   数字 4/5 是空箱，不作为豆子目标。
    const int targetDigit = targetDigitForBean(bean.bean);  // 豆子类别 -> 目标数字
    Detection box = chooseTargetBox(detections, targetDigit); // 在检测结果中找目标数字箱
    if (box.digit == 0)
    {
        // OpenCV 默认 putText 不支持中文，调试画面用 ASCII 状态码显示。
        decision.reason = "no_target_box";              // 未找到目标数字箱
        stableCount_ = 0;                               // 重置稳定计数
        return decision;
    }

    // 第三步：连续稳定后才输出有效决策，减少乱跳。
    // YOLO 单帧可能会偶尔误识别数字。
    // 所以要求同一个目标数字连续出现 minStableFrames 帧。
    if (targetDigit == lastTargetDigit_) stableCount_++;   // 目标未变，计数加一
    else
    {
        lastTargetDigit_ = targetDigit;                    // 目标切换
        stableCount_ = 1;                                  // 重置为 1
    }

    decision.valid = stableCount_ >= config_.minStableFrames;  // 稳定帧数达标才有效
    decision.bean = bean.bean;                             // 当前豆子类别
    decision.targetDigit = targetDigit;                    // 目标数字箱编号

    // 目标点先用数字框中心。
    // 后续如果相机固定且要控制吊具，可以在这里换成：
    //   箱口中心
    //   箱子上边缘中心
    //   标定后的世界坐标
    decision.targetCenter = cv::Point2f(box.box.x + box.box.width * 0.5f,   // 框中心 x
                                        box.box.y + box.box.height * 0.5f); // 框中心 y
    decision.yawErrorPixel = decision.targetCenter.x - imageSize.width * 0.5f; // 水平像素偏差
    // OpenCV 默认 putText 不支持中文，调试画面用 ASCII 状态码显示。
    decision.reason = decision.valid ? "target_stable" : "wait_stable";  // 稳定/等待中
    return decision;
}
