#ifndef TASK_PLANNER_H
#define TASK_PLANNER_H

#include "/home/zst/zst/include/ImgProcessing/VisionTypes.h"
#include <opencv2/core.hpp>
#include <vector>

struct PlannerConfig
{
    // 目标连续稳定多少帧后才认为可发送给电控。
    int minStableFrames = 3;

    // 中心误差死区，后续转 yaw/pitch 时会用到。
    float centerDeadband = 20.0f;
};

class TaskPlanner
{
public:
    explicit TaskPlanner(const PlannerConfig &config);

    // 根据豆子识别结果和数字箱识别结果，生成视觉决策。
    // 比赛规则写在这里：
    //   黄豆 -> 1号箱
    //   绿豆 -> 2号箱
    //   白芸豆 -> 3号箱
    //   4/5号箱为空箱，不作为目标箱
    VisionDecision update(const std::vector<Detection> &detections, const cv::Size &imageSize);

private:
    // 当前策略：选置信度最高的豆子。
    Detection chooseBestBean(const std::vector<Detection> &detections) const;

    // 找到目标数字对应的箱子。
    Detection chooseTargetBox(const std::vector<Detection> &detections, int digit) const;

    PlannerConfig config_;
    int stableCount_ = 0;
    int lastTargetDigit_ = 0;
};

#endif // TASK_PLANNER_H
