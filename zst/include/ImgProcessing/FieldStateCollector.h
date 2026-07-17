#ifndef FIELD_STATE_COLLECTOR_H
#define FIELD_STATE_COLLECTOR_H

#include "ImgProcessing/FieldState.h"
#include "ImgProcessing/VisionTypes.h"
#include <array>
#include <string>
#include <vector>

struct FieldStateCollectorConfig
{
    // 每个角度累计多少帧后，才提交一次当前角度的稳定结果。
    // 不建议一帧就保存，因为 YOLO 单帧可能偶尔误识别。
    // 例如这里设置 20，表示一个角度连续看 20 帧后再投票。
    int voteFramesPerAngle = 20;    // 每个观察角度需要累计的帧数

    // 某个类别在当前角度至少出现多少帧才可信。
    // 例如 20 帧里至少出现 10 帧，才会被加入本角度结果。
    int minHitsPerAngle = 10;        // 当前角度内最少出现次数阈值

    // 数字箱不能按照“第一次识别到的先后顺序”零散写入数组。
    // 每个角度必须至少凑齐这么多个稳定、未保存且互不重复的新数字，才允许把该角度
    // 的全部稳定数字按平均X坐标从左到右整批写入。设置为4表示第一角度必须先凑齐4个；
    // 已保存4个、总容量只剩1个时，会自动把本轮要求降为1。
    int minNewDigitsPerCommit = 4;

    // true：当前相机画面固定对应物理place1~place4，place5完全不可见。
    // 第一轮恰好得到4个稳定不同数字后，按平均X从左到右写入place1~4，
    // 再利用数字1~5总和为15推断place5，不需要切换第二角度。
    bool inferPlace5FromFirstFour = true;

    // 豆子一次提交前至少需要多少个稳定、未保存的新类别。
    // A组设为3：必须黄豆、绿豆、白芸豆全部稳定出现后，才按平均X整批写入；
    // 少任何一个都不允许部分保存。B组设为1：中心豆子稳定后立即保存并发送。
    int minNewBeansPerCommit = 3;

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
    // true 表示刚刚完成了一个角度的投票核对；核对不通过时addedCount仍为0。
    bool committed = false;     // 本帧是否触发了一次角度核对

    // 当前角度新增了多少个以前没保存过的目标。
    int addedCount = 0;         // 本次提交新增的目标数量
};

class FieldStateCollector
{
public:
    explicit FieldStateCollector(const FieldStateCollectorConfig &config);

    // 豆子阶段调用：A组必须凑齐全部稳定豆子后按多帧平均X整批保存；
    // B组由配置只选最稳定的中心候选。已保存类型不会重复占用bean_place。
    // 同一帧同一豆子类别最多计票一次，重复框只保留置信度最高的一个。
    AngleCommitResult addBeanFrame(const std::vector<Detection> &detections);

    // 数字箱阶段调用：一个角度累计若干帧后，先检查稳定新数字数量是否达到门槛；
    // 达标后才依据多帧平均X坐标从左到右整批保存，未达标时本轮一个也不保存。
    // 同一数字在同一帧最多计票一次，避免重复框把命中次数和平均X坐标带偏。
    AngleCommitResult addBoxFrame(const std::vector<Detection> &detections);

    const FieldState &state() const;     // 获取当前已保存的整场状态
    bool beanReady() const;              // 三个豆子位置是否已全部填写
    bool boxReady() const;               // 五个箱子位置是否已全部填写
    bool done() const;                   // 整场识别是否完成（豆子+箱子都就绪）
    int beanCount() const;               // 当前已保存的豆子数量
    int boxCount() const;                // 当前已保存的数字箱数量
    void reset();                        // 清空所有状态，重新开始

    // 从磁盘断点恢复已经生成并准备发送的稳定结果。
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
    // A组只有稳定新豆子数量达到minNewBeansPerCommit时才按X整批写入；
    // B组只确认中心最高票候选并逐个保存。
    AngleCommitResult commitBeanAngle();

    // 当前数字箱角度累计够 voteFramesPerAngle 后调用。
    // 只有稳定新数字数量达到minNewDigitsPerCommit（或剩余容量）时，才按X整批追加。
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

    // 终端日志去重键：相同的“未凑齐/重复/无候选”状态只提示一次。
    // 当前候选类别、已保存数量或阶段发生变化时，键随之变化，才重新输出提示。
    std::string lastBeanStatusLogKey_;
    std::string lastBoxStatusLogKey_;
};

#endif // FIELD_STATE_COLLECTOR_H
