#include "/home/zst/zst/include/ImgProcessing/TaskPlanner.h"
#include <algorithm>

TaskPlanner::TaskPlanner(const PlannerConfig &config) : config_(config) {}

Detection TaskPlanner::chooseBestBean(const std::vector<Detection> &detections) const
{
    Detection best;
    for (const auto &d : detections)
        if (d.kind == TargetKind::Bean && d.score > best.score) best = d;
    return best;
}

Detection TaskPlanner::chooseTargetBox(const std::vector<Detection> &detections, int digit) const
{
    Detection best;
    for (const auto &d : detections)
        if (d.kind == TargetKind::DigitBox && d.digit == digit && d.score > best.score) best = d;
    return best;
}

VisionDecision TaskPlanner::update(const std::vector<Detection> &detections, const cv::Size &imageSize)
{
    VisionDecision decision;

    // 第一步：先确定当前看到的豆子类别。
    // 目前策略很简单：选置信度最高的豆子框。
    // 如果以后机械一次只处理某个取货位，可以在这里加入 ROI 区域限制。
    Detection bean = chooseBestBean(detections);
    if (bean.bean == BeanType::Unknown)
    {
        decision.reason = "未识别到豆子";
        stableCount_ = 0;
        return decision;
    }

    // 第二步：按规则找到该豆子应该去的数字箱。
    // 规则来自比赛手册：
    //   黄豆必须放入数字 1 货箱；
    //   绿豆必须放入数字 2 货箱；
    //   白芸豆必须放入数字 3 货箱；
    //   数字 4/5 是空箱，不作为豆子目标。
    const int targetDigit = targetDigitForBean(bean.bean);
    Detection box = chooseTargetBox(detections, targetDigit);
    if (box.digit == 0)
    {
        decision.reason = "未找到目标数字箱";
        stableCount_ = 0;
        return decision;
    }

    // 第三步：连续稳定后才输出有效决策，减少乱跳。
    // YOLO 单帧可能会偶尔误识别数字。
    // 所以要求同一个目标数字连续出现 minStableFrames 帧。
    if (targetDigit == lastTargetDigit_) stableCount_++;
    else
    {
        lastTargetDigit_ = targetDigit;
        stableCount_ = 1;
    }

    decision.valid = stableCount_ >= config_.minStableFrames;
    decision.bean = bean.bean;
    decision.targetDigit = targetDigit;

    // 目标点先用数字框中心。
    // 后续如果相机固定且要控制吊具，可以在这里换成：
    //   箱口中心
    //   箱子上边缘中心
    //   标定后的世界坐标
    decision.targetCenter = cv::Point2f(box.box.x + box.box.width * 0.5f,
                                        box.box.y + box.box.height * 0.5f);
    decision.yawErrorPixel = decision.targetCenter.x - imageSize.width * 0.5f;
    decision.reason = decision.valid ? "目标稳定" : "等待稳定";
    return decision;
}
