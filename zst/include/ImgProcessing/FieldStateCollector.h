#ifndef FIELD_STATE_COLLECTOR_H
#define FIELD_STATE_COLLECTOR_H

#include "ImgProcessing/FieldState.h"
#include "ImgProcessing/VisionTypes.h"
#include <array>
#include <map>
#include <string>
#include <vector>

struct FieldStateCollectorConfig
{
    // 每个角度累计多少帧后，才提交一次当前角度的稳定结果。
    // 不建议一帧就保存，因为 YOLO 单帧可能偶尔误识别。
    // 例如这里设置 20，表示一个角度连续看 20 帧后再投票。
    int voteFramesPerAngle = 20;    // 每个观察角度需要累计的帧数

    // B组中心豆子是单目标识别，仍按类别在一轮中的命中次数判断稳定性。
    int minHitsPerAngle = 10;

    // A组豆子和A/B数字必须在同一帧出现完整集合，并按X排序形成顺序票。
    // 例如20帧中至少15帧的完整排列完全相同，才允许保存。
    int minConsistentOrderFrames = 15;

    // false（A组）：只统计同帧3豆完整X顺序票。
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

    // 豆子阶段调用：A组仅对同帧完整出现的3种豆子按X排序并投票；
    // B组由配置只选最稳定的中心候选。已保存类型不会重复占用bean_place。
    // 同一帧同一豆子类别最多计票一次，重复框只保留置信度最高的一个。
    AngleCommitResult addBeanFrame(const std::vector<Detection> &detections);

    // 数字箱阶段调用：仅当同一帧恰好出现4个不同数字时，才按X排序形成一张顺序票；
    // 一轮内相同顺序达到阈值后写入place1~4，并用1~5总和15推断place5。
    // 同一数字在同一帧最多保留一个框，避免重复框破坏完整排列。
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
        // 仅供B组中心豆子使用：id表示豆子编码，1黄豆，2绿豆，3白芸豆。
        int id = 0;

        // 当前角度的若干帧中，这个 id 被识别到的次数。
        int count = 0;

        // 当前角度中，这个 id 的检测框中心 x 坐标总和。
        // 用它计算平均 x，实现“当前角度内从右到左排序”。
        float xSum = 0.0f;

        float averageX() const
        {
            return count > 0 ? xSum / count : 0.0f;
        }
    };

    void resetBeanAngle();
    void resetBoxAngle();

    // 当前豆子累计够 voteFramesPerAngle 后调用：A组核对完整排列票，B组核对单目标票。
    AngleCommitResult commitBeanAngle();

    // 当前数字箱累计够 voteFramesPerAngle 后调用；固定执行“前四位识别+place5推断”。
    AngleCommitResult commitBoxAngle();

    FieldStateCollectorConfig config_;

    // 最终要发送给电控的整场状态。
    FieldState state_;

    // 当前豆子角度已经累计了多少帧。
    int beanFrameCount_ = 0;

    // 当前箱子角度已经累计了多少帧。
    int boxFrameCount_ = 0;

    // A组豆子/数字在首次看到完整目标集合后才启动20帧窗口。
    bool beanOrderVotingStarted_ = false;
    bool boxOrderVotingStarted_ = false;

    // 下一个要写入的豆子固定位置下标，范围 0~2。
    int nextBeanIndex_ = 0;

    // 下一个要写入的箱子固定位置下标，范围 0~4。
    int nextBoxIndex_ = 0;

    // 下标 1~3 分别表示黄豆、绿豆、白芸豆。
    std::array<CandidateStat, 4> beanStats_{};

    // A组豆子完整帧的从右到左排列票；只有同帧3类齐全时才记一票。
    std::map<std::array<int, 3>, int> beanOrderVotes_;

    // 数字完整帧的从右到左排列票；只有同帧恰好4个不同数字时才记一票。
    std::map<std::array<int, 4>, int> boxOrderVotes_;

    // 记录某种豆子是否已经保存过。
    // 这样多角度重叠识别时，同一种豆子不会重复占位置。
    std::array<bool, 4> seenBeans_{};

    // 终端日志去重键：相同的“未凑齐/重复/无候选”状态只提示一次。
    // 当前候选类别、已保存数量或阶段发生变化时，键随之变化，才重新输出提示。
    std::string lastBeanStatusLogKey_;
    std::string lastBoxStatusLogKey_;
};

#endif // FIELD_STATE_COLLECTOR_H
