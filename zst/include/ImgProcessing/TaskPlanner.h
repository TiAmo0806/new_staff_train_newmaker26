#ifndef TASK_PLANNER_H
#define TASK_PLANNER_H

#include "ImgProcessing/VisionTypes.h"
#include <opencv2/core.hpp>
#include <vector>

struct PlannerConfig
{
    // 目标连续稳定多少帧后才认为可发送给电控。
    int minStableFrames = 3;    // 连续稳定帧数阈值，低于此值不输出 valid 决策
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

    PlannerConfig config_;      // 规划器配置的只读副本
    int stableCount_ = 0;       // 当前目标连续出现的帧数
    int lastTargetDigit_ = 0;   // 上一帧的目标数字，用于判断是否切换目标
};

#endif // TASK_PLANNER_H
