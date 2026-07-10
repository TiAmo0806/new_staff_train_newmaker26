#ifndef FIELD_STATE_COLLECTOR_H
#define FIELD_STATE_COLLECTOR_H

#include "ImgProcessing/FieldState.h"
#include "ImgProcessing/VisionTypes.h"
#include <array>
#include <vector>

struct FieldStateCollectorConfig
{
    // 每个角度累计多少帧后，才提交一次当前角度的稳定结果。
    // 不建议一帧就保存，因为 YOLO 单帧可能偶尔误识别。
    // 例如这里设置 20，表示一个角度连续看 20 帧后再投票。
    int voteFramesPerAngle = 20;

    // 某个类别在当前角度至少出现多少帧才可信。
    // 例如 20 帧里至少出现 6 帧，才会被加入本角度结果。
    int minHitsPerAngle = 6;
};

struct AngleCommitResult
{
    // true 表示刚刚完成了一个角度的投票并提交了结果。
    bool committed = false;

    // 当前角度新增了多少个以前没保存过的目标。
    int addedCount = 0;
};

class FieldStateCollector
{
public:
    explicit FieldStateCollector(const FieldStateCollectorConfig &config);

    // 豆子阶段调用：累计若干帧后，按画面从左到右保存新豆子。
    // 适合你的“两次角度看豆子”方案。
    // 已经保存过的豆子类型会跳过，不会重复占用 bean_place。
    AngleCommitResult addBeanFrame(const std::vector<Detection> &detections);

    // 数字箱阶段调用：累计若干帧后，按画面从左到右保存新数字。
    // 适合你的“三个角度看箱子”方案。
    // 例如第一次看到 2-1-3，第二次看到 1-3-4，
    // 第二次的 1 和 3 会被跳过，只把 4 放到下一个 box_place。
    AngleCommitResult addBoxFrame(const std::vector<Detection> &detections);

    const FieldState &state() const;
    bool beanReady() const;
    bool boxReady() const;
    bool done() const;
    int beanCount() const;
    int boxCount() const;
    void reset();

private:
    struct CandidateStat
    {
        // 豆子阶段：id 表示豆子编码，1黄豆，2绿豆，3白芸豆。
        // 箱子阶段：id 表示数字，1~5。
        int id = 0;

        // 当前角度的若干帧中，这个 id 被识别到的次数。
        int count = 0;

        // 当前角度中，这个 id 的检测框中心 x 坐标总和。
        // 用它计算平均 x，实现“当前角度内从左到右排序”。
        float xSum = 0.0f;

        float averageX() const
        {
            return count > 0 ? xSum / count : 0.0f;
        }
    };

    void resetBeanAngle();
    void resetBoxAngle();

    // 当前豆子角度累计够 voteFramesPerAngle 后调用。
    // 会把本角度稳定出现的豆子按 x 从左到右追加到 FieldState。
    AngleCommitResult commitBeanAngle();

    // 当前数字箱角度累计够 voteFramesPerAngle 后调用。
    // 会把本角度稳定出现的数字按 x 从左到右追加到 FieldState。
    AngleCommitResult commitBoxAngle();

    FieldStateCollectorConfig config_;

    // 最终要发送给电控的整场状态。
    FieldState state_;

    // 当前豆子角度已经累计了多少帧。
    int beanFrameCount_ = 0;

    // 当前箱子角度已经累计了多少帧。
    int boxFrameCount_ = 0;

    // 下一个要写入的豆子固定位置下标，范围 0~2。
    int nextBeanIndex_ = 0;

    // 下一个要写入的箱子固定位置下标，范围 0~4。
    int nextBoxIndex_ = 0;

    // 下标 1~3 分别表示黄豆、绿豆、白芸豆。
    std::array<CandidateStat, 4> beanStats_{};

    // 下标 1~5 分别表示数字 1~5。
    std::array<CandidateStat, 6> boxStats_{};

    // 记录某种豆子是否已经保存过。
    // 这样多角度重叠识别时，同一种豆子不会重复占位置。
    std::array<bool, 4> seenBeans_{};

    // 记录某个数字是否已经保存过。
    // 例如数字 1 在上一个角度已经保存，后续再看到 1 就跳过。
    std::array<bool, 6> seenDigits_{};
};

#endif // FIELD_STATE_COLLECTOR_H
