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
    int voteFramesPerAngle = 20;    // 每个观察角度需要累计的帧数

    // 某个类别在当前角度至少出现多少帧才可信。
    // 例如 20 帧里至少出现 6 帧，才会被加入本角度结果。
    int minHitsPerAngle = 6;        // 当前角度内最少出现次数阈值

    // 每次豆子角度提交最多接受几个”新豆子”。
    // 队伍A一次可能看到多个豆子，使用3；队伍B要求”识别一个、发送一个”，使用1。
    // 这个限制只影响豆子，不影响数字箱；小于等于0表示不限制。
    int maxNewBeansPerCommit = 3;   // 单次角度提交最多新增的豆子数

    // false（A组）：稳定候选按X从左到右全部处理。
    // true（B组）：只处理投票次数最多的一个中心候选；若它已识别过，本轮不改选其他豆子。
    bool selectMostFrequentBeanOnly = false;
};

struct AngleCommitResult
{
    // true 表示刚刚完成了一个角度的投票并提交了结果。
    bool committed = false;     // 本帧是否触发了一次角度提交

    // 当前角度新增了多少个以前没保存过的目标。
    int addedCount = 0;         // 本次提交新增的目标数量
};

class FieldStateCollector
{
public:
    explicit FieldStateCollector(const FieldStateCollectorConfig &config);

    // 豆子阶段调用：A组按画面从左到右保存；B组由配置只选最稳定的中心候选。
    // 已经保存过的豆子类型会跳过，不会重复占用 bean_place。
    AngleCommitResult addBeanFrame(const std::vector<Detection> &detections);

    // 数字箱阶段调用：累计若干帧后，按画面从左到右保存新数字。
    // 适合你的“三个角度看箱子”方案。
    // 例如第一次看到 2-1-3，第二次看到 1-3-4，
    // 第二次的 1 和 3 会被跳过，只把 4 放到下一个 box_place。
    AngleCommitResult addBoxFrame(const std::vector<Detection> &detections);

    const FieldState &state() const;     // 获取当前已保存的整场状态
    bool beanReady() const;              // 三个豆子位置是否已全部填写
    bool boxReady() const;               // 五个箱子位置是否已全部填写
    bool done() const;                   // 整场识别是否完成（豆子+箱子都就绪）
    int beanCount() const;               // 当前已保存的豆子数量
    int boxCount() const;                // 当前已保存的数字箱数量
    void reset();                        // 清空所有状态，重新开始

    // 从磁盘断点恢复已经“成功发送过”的稳定结果。
    // 函数会重新建立nextBeanIndex_、nextBoxIndex_以及豆子/数字去重表，
    // 不会恢复上次尚未完成的20帧临时投票；临时投票从0重新累计更安全。
    void restoreState(const FieldState &savedState);

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
