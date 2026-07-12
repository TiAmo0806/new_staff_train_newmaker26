#include "ImgProcessing/FieldStateCollector.h"
#include <algorithm>

namespace
{
// 计算检测框中心点的 x 坐标。
// 当前角度内需要按“从左到右”保存，所以只需要比较 x。
float detectionCenterX(const Detection &d)
{
    return static_cast<float>(d.box.x) + static_cast<float>(d.box.width) * 0.5f;
}

// 把串口用的豆子编码转回 BeanType。
// 1 黄豆，2 绿豆，3 白芸豆，其他都当未知。
BeanType decodeBeanType(int id)
{
    if (id == 1) return BeanType::Soybean;
    if (id == 2) return BeanType::MungBean;
    if (id == 3) return BeanType::WhiteKidneyBean;
    return BeanType::Unknown;
}
}

FieldStateCollector::FieldStateCollector(const FieldStateCollectorConfig &config)
    : config_(config)
{
    // 构造后立即清空所有状态，保证第一次使用时是干净的。
    reset();
}

void FieldStateCollector::reset()
{
    // 清空最终状态。
    state_ = FieldState{};

    // 清空当前角度帧数。
    beanFrameCount_ = 0;
    boxFrameCount_ = 0;

    // 下一个写入位置重新从 0 开始。
    nextBeanIndex_ = 0;
    nextBoxIndex_ = 0;

    // 清空“已经见过”的记录。
    seenBeans_ = {};
    seenDigits_ = {};

    // 清空当前角度投票桶。
    resetBeanAngle();
    resetBoxAngle();
}

void FieldStateCollector::resetBeanAngle()
{
    // 只清空当前豆子角度的临时投票，不影响已经保存到 state_ 的结果。
    beanFrameCount_ = 0;
    beanStats_ = {};
    for (int i = 0; i < static_cast<int>(beanStats_.size()); ++i)
    {
        beanStats_[i].id = i;
    }
}

void FieldStateCollector::resetBoxAngle()
{
    // 只清空当前箱子角度的临时投票，不影响已经保存到 state_ 的结果。
    boxFrameCount_ = 0;
    boxStats_ = {};
    for (int i = 0; i < static_cast<int>(boxStats_.size()); ++i)
    {
        boxStats_[i].id = i;
    }
}

AngleCommitResult FieldStateCollector::addBeanFrame(const std::vector<Detection> &detections)
{
    // 三个豆子位置已经收集完成后，后续豆子帧直接忽略。
    if (state_.beanReady)
    {
        return {};
    }

    for (const auto &d : detections)
    {
        // 豆子阶段只看豆子框，数字箱框全部跳过。
        if (d.kind != TargetKind::Bean)
        {
            continue;
        }

        // 把 BeanType 转成 1~3 的编码，方便作为数组下标统计。
        const int code = static_cast<int>(encodeBeanType(d.bean));
        if (code < 1 || code > 3)
        {
            continue;
        }

        // 当前角度中，这种豆子又出现了一次。
        beanStats_[code].count++;

        // 累加 x 坐标，后面用平均 x 决定从左到右顺序。
        beanStats_[code].xSum += detectionCenterX(d);
    }

    // 当前角度累计一帧。
    beanFrameCount_++;

    // 还没达到投票帧数，不提交结果。
    if (beanFrameCount_ < config_.voteFramesPerAngle)
    {
        return {};
    }

    // 达到投票帧数，提交当前角度稳定结果。
    return commitBeanAngle();
}

AngleCommitResult FieldStateCollector::addBoxFrame(const std::vector<Detection> &detections)
{
    // 五个数字箱位置已经收集完成后，后续箱子帧直接忽略。
    if (state_.boxReady)
    {
        return {};
    }

    for (const auto &d : detections)
    {
        // 箱子阶段只看数字箱框，豆子框全部跳过。
        if (d.kind != TargetKind::DigitBox || d.digit < 1 || d.digit > 5)
        {
            continue;
        }

        // 当前角度中，这个数字又出现了一次。
        boxStats_[d.digit].count++;

        // 累加 x 坐标，后面用平均 x 决定从左到右顺序。
        boxStats_[d.digit].xSum += detectionCenterX(d);
    }

    // 当前角度累计一帧。
    boxFrameCount_++;

    // 还没达到投票帧数，不提交结果。
    if (boxFrameCount_ < config_.voteFramesPerAngle)
    {
        return {};
    }

    return commitBoxAngle();
}

AngleCommitResult FieldStateCollector::commitBeanAngle()
{
    AngleCommitResult result;
    result.committed = true;

    // 先筛出当前角度内“出现次数足够多”的豆子。
    // 出现次数太少的结果可能是误检，不保存。
    std::vector<CandidateStat> candidates;
    for (int id = 1; id <= 3; ++id)
    {
        if (beanStats_[id].count >= config_.minHitsPerAngle)
        {
            candidates.push_back(beanStats_[id]);
        }
    }

    // 当前角度内部按画面从左到右排序。
    // 这样同一个角度里看到 2-1-3，就会按 2、1、3 的顺序写入位置。
    std::sort(candidates.begin(), candidates.end(), [](const CandidateStat &a, const CandidateStat &b) {
        return a.averageX() < b.averageX();
    });

    for (const auto &candidate : candidates)
    {
        // 队伍B要求一次只确认一个新豆子。限制在这里实现后，
        // 即使同一画面同时出现多个豆子，也只保存排序后的第一个新类别；
        // 其余候选不会丢失为永久结果，而是在下一个观察阶段重新累计20帧再判断。
        if (config_.maxNewBeansPerCommit > 0 &&
            result.addedCount >= config_.maxNewBeansPerCommit)
        {
            break;
        }

        // 如果这种豆子之前已经保存过，说明这是多角度重叠识别，跳过。
        if (seenBeans_[candidate.id])
        {
            continue;
        }

        // 三个豆子位置已经写满，不再保存。
        if (nextBeanIndex_ >= static_cast<int>(state_.beanPlaces.size()))
        {
            break;
        }

        // 把当前新豆子写入下一个 bean_place。
        state_.beanPlaces[nextBeanIndex_] = decodeBeanType(candidate.id);
        seenBeans_[candidate.id] = true;
        nextBeanIndex_++;
        result.addedCount++;
    }

    // 三个位置都写满，豆子区识别完成。
    state_.beanReady = nextBeanIndex_ >= static_cast<int>(state_.beanPlaces.size());

    // 一个角度提交完后，清空临时投票，准备下一个角度。
    resetBeanAngle();
    return result;
}

AngleCommitResult FieldStateCollector::commitBoxAngle()
{
    AngleCommitResult result;
    result.committed = true;

    // 先筛出当前角度内“出现次数足够多”的数字。
    // 出现次数太少的数字可能是误检，不保存。
    std::vector<CandidateStat> candidates;
    for (int digit = 1; digit <= 5; ++digit)
    {
        if (boxStats_[digit].count >= config_.minHitsPerAngle)
        {
            candidates.push_back(boxStats_[digit]);
        }
    }

    // 当前角度内部按画面从左到右排序。
    // 例如当前角度看到 1-3-4，就按 1、3、4 的顺序处理。
    std::sort(candidates.begin(), candidates.end(), [](const CandidateStat &a, const CandidateStat &b) {
        return a.averageX() < b.averageX();
    });

    for (const auto &candidate : candidates)
    {
        // 如果这个数字之前已经保存过，说明它是重叠区域重复看到的数字，跳过。
        if (seenDigits_[candidate.id])
        {
            continue;
        }

        // 五个箱子位置已经写满，不再保存。
        if (nextBoxIndex_ >= static_cast<int>(state_.boxPlaces.size()))
        {
            break;
        }

        // 把当前新数字写入下一个 box_place。
        state_.boxPlaces[nextBoxIndex_] = candidate.id;
        seenDigits_[candidate.id] = true;
        nextBoxIndex_++;
        result.addedCount++;
    }

    // 五个位置都写满，箱子区识别完成。
    state_.boxReady = nextBoxIndex_ >= static_cast<int>(state_.boxPlaces.size());

    // 一个角度提交完后，清空临时投票，准备下一个角度。
    resetBoxAngle();
    return result;
}

const FieldState &FieldStateCollector::state() const
{
    return state_;
}

bool FieldStateCollector::beanReady() const
{
    return state_.beanReady;
}

bool FieldStateCollector::boxReady() const
{
    return state_.boxReady;
}

bool FieldStateCollector::done() const
{
    return state_.valid();
}

int FieldStateCollector::beanCount() const
{
    return nextBeanIndex_;
}

int FieldStateCollector::boxCount() const
{
    return nextBoxIndex_;
}
