#include "ImgProcessing/FieldStateCollector.h"
#include <algorithm>
#include <iostream>
#include <sstream>

namespace
{
// 计算检测框中心点的 x 坐标。A组豆子按X升序（左到右），数字按X降序（右到左）。
float detectionCenterX(const Detection &d)
{
    return static_cast<float>(d.box.x) + static_cast<float>(d.box.width) * 0.5f; // 框左边界 + 半宽 = 中心 x
}

// 把串口用的豆子编码转回 BeanType。
// 1 黄豆，2 绿豆，3 白芸豆，其他都当未知。
BeanType decodeBeanType(int id)
{
    if (id == 1) return BeanType::Soybean;          // 编码 1 -> 黄豆
    if (id == 2) return BeanType::MungBean;         // 编码 2 -> 绿豆
    if (id == 3) return BeanType::WhiteKidneyBean;  // 编码 3 -> 白芸豆
    return BeanType::Unknown;                        // 未知编码
}

const char *beanChineseName(int id)
{
    if (id == 1) return "黄豆";
    if (id == 2) return "绿豆";
    if (id == 3) return "白芸豆";
    return "未知豆子";
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

    // 新比赛/切队时允许所有关键状态重新输出一次。
    // 单纯完成一轮20帧投票不会清空这些键，从而抑制相同失败信息反复刷屏。
    lastBeanStatusLogKey_.clear();
    lastBoxStatusLogKey_.clear();

    // 清空当前角度投票桶。
    resetBeanAngle();
    resetBoxAngle();
}

void FieldStateCollector::restoreState(const FieldState &savedState)
{
    // 先回到完全干净的状态，避免恢复数据与构造阶段的默认值混在一起。
    reset();

    // beanPlaces按保存顺序恢复。只接受1~3范围内且尚未出现过的有效豆子，
    // 即使进度文件被手工改坏，也不会把未知值或重复豆子写入正式状态。
    for (BeanType bean : savedState.beanPlaces)
    {
        const int code = static_cast<int>(encodeBeanType(bean));
        if (code < 1 || code > 3 || seenBeans_[code]) continue;
        if (nextBeanIndex_ >= static_cast<int>(state_.beanPlaces.size())) break;

        state_.beanPlaces[nextBeanIndex_] = bean;
        seenBeans_[code] = true;
        ++nextBeanIndex_;
    }

    // boxPlaces同样只恢复1~5范围内且不重复的数字。
    // 当前业务约定每个数字只出现一次，因此重复值应视为损坏数据并跳过。
    std::array<bool, 6> restoredDigits{};
    for (int digit : savedState.boxPlaces)
    {
        if (digit < 1 || digit > 5 || restoredDigits[digit]) continue;
        if (nextBoxIndex_ >= static_cast<int>(state_.boxPlaces.size())) break;

        state_.boxPlaces[nextBoxIndex_] = digit;
        restoredDigits[digit] = true;
        ++nextBoxIndex_;
    }

    // ready不直接相信文件中的布尔值，而是根据实际恢复出的有效元素重新计算。
    state_.beanReady = nextBeanIndex_ >= static_cast<int>(state_.beanPlaces.size());
    state_.boxReady = nextBoxIndex_ >= static_cast<int>(state_.boxPlaces.size());

    // 上次中途累计但没有形成稳定结果的投票不落盘，恢复后从新画面重新投票。
    resetBeanAngle();
    resetBoxAngle();
}

void FieldStateCollector::resetBeanAngle()
{
    // 只清空当前豆子角度的临时投票，不影响已经保存到 state_ 的结果。
    beanFrameCount_ = 0;                                // 重置当前角度帧数
    beanOrderVotingStarted_ = false;
    beanOrderVotes_.clear();                            // 清空A组完整排列票
    beanPairOrderVotes_.clear();                        // 清空A组前两豆推测票
    beanStats_ = {};                                    // 清空投票桶
    for (int i = 0; i < static_cast<int>(beanStats_.size()); ++i)
    {
        beanStats_[i].id = i;                           // 还原候选编号（下标即编号）
    }
}

void FieldStateCollector::resetBoxAngle()
{
    // 只清空当前箱子角度的临时投票，不影响已经保存到 state_ 的结果。
    boxFrameCount_ = 0;                                 // 重置当前角度帧数
    boxOrderVotingStarted_ = false;
    boxOrderVotes_.clear();                             // 清空4数字完整排列票
}

AngleCommitResult FieldStateCollector::addBeanFrame(const std::vector<Detection> &detections)
{
    // 三个豆子位置已经收集完成后，后续豆子帧直接忽略。
    if (state_.beanReady)
    {
        return {};                                      // 已完成，无需再累加
    }

    // 同一帧可能产生同类别重复框。每个豆子类别只选择置信度最高的一个框；
    // A组用它形成三豆直接票或两豆推测票，B组用它累计中心单豆类别票。
    std::array<const Detection *, 4> bestDetectionByBean{};
    for (const auto &d : detections)
    {
        // 豆子阶段只看豆子框，数字箱框全部跳过。
        if (d.kind != TargetKind::Bean)
        {
            continue;                                   // 跳过非豆子检测
        }

        // 把 BeanType 转成 1~3 的编码，方便作为数组下标统计。
        const int code = static_cast<int>(encodeBeanType(d.bean)); // 1=黄豆, 2=绿豆, 3=白芸豆
        if (code < 1 || code > 3)
        {
            continue;                                   // 未知豆子类型，跳过
        }

        const Detection *&best = bestDetectionByBean[code];
        if (best == nullptr || d.score > best->score)
            best = &d;                                  // 同类重复框只保留最高置信度者
    }

    if (config_.selectMostFrequentBeanOnly)
    {
        // B组只接收上层筛选出的中心豆子，继续使用单类别命中次数投票。
        for (int code = 1; code <= 3; ++code)
        {
            const Detection *best = bestDetectionByBean[code];
            if (best == nullptr) continue;
            beanStats_[code].count++;
            beanStats_[code].xSum += detectionCenterX(*best);
        }
        beanFrameCount_++;                              // B组单目标进入阶段后立即累计
    }
    else
    {
        int distinctBeanCount = 0;
        for (int code = 1; code <= 3; ++code)
            if (bestDetectionByBean[code] != nullptr) ++distinctBeanCount;

        if (!beanOrderVotingStarted_ && distinctBeanCount >= 2)
            beanOrderVotingStarted_ = true;             // 首个至少2目标帧启动20帧窗口
        if (!beanOrderVotingStarted_) return {};         // 尚未出现两个不同豆子，不消耗窗口

        if (distinctBeanCount == 3)
        {
            std::array<const Detection *, 3> completeBeans{
                bestDetectionByBean[1], bestDetectionByBean[2], bestDetectionByBean[3]
            };
            std::sort(completeBeans.begin(), completeBeans.end(),
                      [](const Detection *a, const Detection *b) {
                          return detectionCenterX(*a) < detectionCenterX(*b);
                      });
            std::array<int, 3> order{};
            for (std::size_t i = 0; i < order.size(); ++i)
                order[i] = static_cast<int>(encodeBeanType(completeBeans[i]->bean));
            beanOrderVotes_[order]++;
        }
        else if (distinctBeanCount == 2)
        {
            std::array<const Detection *, 2> visibleBeans{};
            std::size_t index = 0;
            for (int code = 1; code <= 3; ++code)
            {
                if (bestDetectionByBean[code] != nullptr)
                    visibleBeans[index++] = bestDetectionByBean[code];
            }
            std::sort(visibleBeans.begin(), visibleBeans.end(),
                      [](const Detection *a, const Detection *b) {
                          return detectionCenterX(*a) < detectionCenterX(*b);
                      });
            const std::array<int, 2> pairOrder{
                static_cast<int>(encodeBeanType(visibleBeans[0]->bean)),
                static_cast<int>(encodeBeanType(visibleBeans[1]->bean))
            };
            beanPairOrderVotes_[pairOrder]++;
        }
        beanFrameCount_++;                              // 启动后缺目标帧也计入20帧窗口
    }

    // 还没达到投票帧数，不提交结果。
    if (beanFrameCount_ < config_.voteFramesPerAngle)
    {
        return {};                                      // 继续累计
    }

    // 达到投票帧数，提交当前角度稳定结果。
    return commitBeanAngle();
}

AngleCommitResult FieldStateCollector::addBoxFrame(const std::vector<Detection> &detections)
{
    // 五个数字箱位置已经收集完成后，后续箱子帧直接忽略。
    if (state_.boxReady)
    {
        return {};                                      // 已完成，无需再累加
    }

    // 同一帧中，同一个数字可能产生多个重复框；每个数字只保留置信度最高的一个，
    // 再检查这一帧是否恰好构成4个不同数字的完整排列。
    std::array<const Detection *, 6> bestDetectionByDigit{};
    for (const auto &d : detections)
    {
        // 箱子阶段只看数字箱框，豆子框全部跳过。
        if (d.kind != TargetKind::DigitBox || d.digit < 1 || d.digit > 5)
        {
            continue;                                   // 跳过非数字箱检测或无效数字
        }

        const Detection *&best = bestDetectionByDigit[d.digit];
        if (best == nullptr || d.score > best->score)
            best = &d;                                  // 同数字重复框只留下置信度最高者
    }

    std::vector<const Detection *> completeDigits;
    for (int digit = 1; digit <= 5; ++digit)
    {
        const Detection *best = bestDetectionByDigit[digit];
        if (best != nullptr) completeDigits.push_back(best);
    }

    const bool complete = completeDigits.size() == 4;
    if (!boxOrderVotingStarted_ && complete)
        boxOrderVotingStarted_ = true;                  // 首个完整4目标帧启动20帧窗口
    if (!boxOrderVotingStarted_) return {};             // 尚未完整出现，不消耗投票窗口

    // 窗口启动后，只有恰好4个不同数字的帧产生顺序票；少于4个或误检出5个均不计票。
    if (complete)
    {
        std::sort(completeDigits.begin(), completeDigits.end(),
                  [](const Detection *a, const Detection *b) {
                      return detectionCenterX(*a) > detectionCenterX(*b);
                  });
        std::array<int, 4> order{};
        for (std::size_t i = 0; i < order.size(); ++i)
            order[i] = completeDigits[i]->digit;
        boxOrderVotes_[order]++;
    }

    boxFrameCount_++;                                   // 启动后缺目标/多目标帧也计入20帧窗口

    // 还没达到投票帧数，不提交结果。
    if (boxFrameCount_ < config_.voteFramesPerAngle)
    {
        return {};                                      // 继续累计
    }

    return commitBoxAngle();
}

AngleCommitResult FieldStateCollector::commitBeanAngle()
{
    AngleCommitResult result;
    result.committed = true;                            // 标记本次调用产生了一次提交

    if (!config_.selectMostFrequentBeanOnly)
    {
        // A组分别统计互斥的三目标直接票和两目标推测票。三目标路径始终优先。
        std::array<int, 3> bestCompleteOrder{};
        int bestCompleteVotes = 0;
        int completeFrames = 0;
        for (const auto &[order, votes] : beanOrderVotes_)
        {
            completeFrames += votes;
            if (votes > bestCompleteVotes)
            {
                bestCompleteOrder = order;
                bestCompleteVotes = votes;
            }
        }

        std::array<int, 2> bestPairOrder{};
        int bestPairVotes = 0;
        int pairFrames = 0;
        for (const auto &[order, votes] : beanPairOrderVotes_)
        {
            pairFrames += votes;
            if (votes > bestPairVotes)
            {
                bestPairOrder = order;
                bestPairVotes = votes;
            }
        }

        std::ostringstream statusKey;
        statusKey << "A:" << completeFrames << ':' << bestCompleteVotes << ':'
                  << bestCompleteOrder[0] << bestCompleteOrder[1] << bestCompleteOrder[2]
                  << ":P:" << pairFrames << ':' << bestPairVotes << ':'
                  << bestPairOrder[0] << bestPairOrder[1];
        const bool shouldLog = statusKey.str() != lastBeanStatusLogKey_;

        std::array<int, 3> selectedOrder{};
        bool usedInference = false;
        if (bestCompleteVotes >= config_.minCompleteBeanOrderFrames)
        {
            selectedOrder = bestCompleteOrder;
        }
        else if (bestPairVotes >= config_.minInferredBeanOrderFrames)
        {
            const int inferredCode = 6 - bestPairOrder[0] - bestPairOrder[1];
            const bool inferenceInvalid =
                bestPairOrder[0] < 1 || bestPairOrder[0] > 3 ||
                bestPairOrder[1] < 1 || bestPairOrder[1] > 3 ||
                bestPairOrder[0] == bestPairOrder[1] ||
                inferredCode < 1 || inferredCode > 3 ||
                inferredCode == bestPairOrder[0] || inferredCode == bestPairOrder[1];
            if (inferenceInvalid)
            {
                std::cerr << "[A组豆子推测] 前两豆编码非法，本轮不保存" << std::endl;
                lastBeanStatusLogKey_ = statusKey.str();
                resetBeanAngle();
                return result;
            }
            selectedOrder = {bestPairOrder[0], bestPairOrder[1], inferredCode};
            usedInference = true;
        }
        else
        {
            if (shouldLog)
            {
                std::cout << "[A组豆子顺序核对] 三目标帧=" << completeFrames
                          << '/' << config_.voteFramesPerAngle
                          << "，最高同序票=" << bestCompleteVotes
                          << '/' << config_.minCompleteBeanOrderFrames
                          << "；两目标帧=" << pairFrames
                          << '/' << config_.voteFramesPerAngle
                          << "，最高前两位同序票=" << bestPairVotes
                          << '/' << config_.minInferredBeanOrderFrames
                          << "，本轮不保存" << std::endl;
            }
            lastBeanStatusLogKey_ = statusKey.str();
            resetBeanAngle();
            return result;
        }

        if (usedInference)
        {
            std::cout << "[A组豆子推测] 通过，" << bestPairVotes << '/'
                      << config_.voteFramesPerAngle << "帧前两豆从左到右一致="
                      << beanChineseName(selectedOrder[0]) << " -> "
                      << beanChineseName(selectedOrder[1])
                      << "，推测place3=" << beanChineseName(selectedOrder[2])
                      << std::endl;
        }
        else
        {
            std::cout << "[A组豆子顺序核对] 直接通过，" << bestCompleteVotes << '/'
                      << config_.voteFramesPerAngle << "帧三豆从左到右一致=";
            for (std::size_t i = 0; i < selectedOrder.size(); ++i)
            {
                if (i != 0) std::cout << " -> ";
                std::cout << beanChineseName(selectedOrder[i]);
            }
            std::cout << std::endl;
        }

        for (std::size_t i = 0; i < selectedOrder.size(); ++i)
        {
            state_.beanPlaces[i] = decodeBeanType(selectedOrder[i]);
            seenBeans_[selectedOrder[i]] = true;
        }
        nextBeanIndex_ = 3;
        result.addedCount = 3;
        state_.beanReady = true;
        lastBeanStatusLogKey_ = statusKey.str();
        resetBeanAngle();
        return result;
    }

    // B组：输入已由上层筛成每帧唯一中心豆子，保持单类别命中次数投票。
    std::vector<CandidateStat> candidates;
    for (int id = 1; id <= 3; ++id)
        if (beanStats_[id].count >= config_.minHitsPerAngle)
            candidates.push_back(beanStats_[id]);

    std::sort(candidates.begin(), candidates.end(), [](const CandidateStat &a,
                                                       const CandidateStat &b) {
        if (a.count != b.count) return a.count > b.count;
        return a.id < b.id;
    });

    std::ostringstream statusKey;
    statusKey << "B:" << nextBeanIndex_ << ':';
    for (const CandidateStat &candidate : candidates)
        statusKey << candidate.id << '-' << candidate.count << ',';
    const bool shouldLog = statusKey.str() != lastBeanStatusLogKey_;

    if (candidates.size() > 1 && candidates[0].count == candidates[1].count)
    {
        if (shouldLog)
            std::cout << "[B组中心豆子核对] 最高票并列，本轮不保存" << std::endl;
        candidates.clear();
    }
    else if (candidates.size() > 1)
        candidates.resize(1);

    if (candidates.empty())
    {
        if (shouldLog)
            std::cout << "[B组中心豆子核对] 没有类别达到单目标命中阈值，本轮不保存"
                      << std::endl;
        lastBeanStatusLogKey_ = statusKey.str();
        resetBeanAngle();
        return result;
    }

    const CandidateStat &candidate = candidates.front();
    if (seenBeans_[candidate.id])
    {
        if (shouldLog)
            std::cout << "[B组豆子保存] " << beanChineseName(candidate.id)
                      << "以前已经保存，本轮不重复保存、不重复发送" << std::endl;
        lastBeanStatusLogKey_ = statusKey.str();
        resetBeanAngle();
        return result;
    }

    if (nextBeanIndex_ < static_cast<int>(state_.beanPlaces.size()))
    {
        state_.beanPlaces[nextBeanIndex_] = decodeBeanType(candidate.id);
        seenBeans_[candidate.id] = true;
        ++nextBeanIndex_;
        result.addedCount = 1;
        state_.beanReady = nextBeanIndex_ >= static_cast<int>(state_.beanPlaces.size());
        std::cout << "[B组豆子保存] 最终确认=" << beanChineseName(candidate.id)
                  << "，命中=" << candidate.count
                  << "，新增=1，完成=" << (state_.beanReady ? "是" : "否")
                  << std::endl;
    }

    lastBeanStatusLogKey_ = statusKey.str();
    resetBeanAngle();
    return result;
}

AngleCommitResult FieldStateCollector::commitBoxAngle()
{
    AngleCommitResult result;
    result.committed = true;                            // 标记本次调用完成了一轮数字核对

    // 当前工作流只保留“识别place1~place4并推断place5”。因此数字阶段必须从空数组开始；
    // 若断点或外部数据带入半成品，宁可拒绝继续，也不能把它当成普通多角度模式追加。
    if (nextBoxIndex_ != 0)
    {
        std::cerr << "[数字核对] 数字缓存不是空状态，无法执行前四位推断，本轮不保存"
                  << std::endl;
        resetBoxAngle();
        return result;
    }

    // 统计20帧中票数最多的完整4数字X顺序。缺目标或多出第5个目标的帧不产生顺序票。
    std::array<int, 4> bestOrder{};
    int bestVotes = 0;
    int completeFrames = 0;
    for (const auto &[order, votes] : boxOrderVotes_)
    {
        completeFrames += votes;
        if (votes > bestVotes)
        {
            bestOrder = order;
            bestVotes = votes;
        }
    }

    std::ostringstream boxStatusKeyBuilder;
    boxStatusKeyBuilder << "order:" << completeFrames << ':' << bestVotes << ':';
    for (int digit : bestOrder) boxStatusKeyBuilder << digit;
    const std::string boxStatusKey = boxStatusKeyBuilder.str();
    const bool shouldLogBoxStatus = boxStatusKey != lastBoxStatusLogKey_;
    if (bestVotes < config_.minConsistentOrderFrames)
    {
        if (shouldLogBoxStatus)
        {
            std::cout << "[数字顺序核对] 完整4目标帧=" << completeFrames
                      << '/' << config_.voteFramesPerAngle
                      << "，最高一致顺序票=" << bestVotes
                      << '/' << config_.minConsistentOrderFrames
                      << "，本轮不保存" << std::endl;
        }
        lastBoxStatusLogKey_ = boxStatusKey;
        resetBoxAngle();
        return result;
    }

    // 数字1~5各出现一次，总和固定为15。推断模式下四个候选已经保证互不重复，
    // 因而15减去四个候选之和就是唯一缺失数字，也就是不可见的place5内容。
    int detectedDigitSum = 0;
    for (int digit : bestOrder)
        detectedDigitSum += digit;
    const int inferredPlace5Digit = 15 - detectedDigitSum;

    const bool inferredDigitInvalid =
        inferredPlace5Digit < 1 || inferredPlace5Digit > 5 ||
        std::any_of(bestOrder.begin(), bestOrder.end(), [&](int digit) {
            return digit == inferredPlace5Digit;
        });
    if (inferredDigitInvalid)
    {
        // 理论上“4个互不重复且均在1~5”不会进入这里；保留防御检查，避免未来改动
        // 放宽候选条件后，把0、负数或重复数字写入正式数组。
        std::cerr << "[数字顺序核对] place5推断非法：四个数字总和="
                  << detectedDigitSum << "，推断值=" << inferredPlace5Digit
                  << "，本轮不保存" << std::endl;
        resetBoxAngle();
        return result;
    }

    std::cout << "[数字顺序核对] 通过，" << bestVotes << '/'
              << config_.voteFramesPerAngle << "帧从右到左一致=";
    for (size_t i = 0; i < bestOrder.size(); ++i)
    {
        if (i != 0) std::cout << " -> ";
        std::cout << bestOrder[i];
    }
    std::cout << std::endl;
    lastBoxStatusLogKey_ = boxStatusKey;

    for (int digit : bestOrder)
    {
        // 五个箱子位置已经写满，不再保存。
        if (nextBoxIndex_ >= static_cast<int>(state_.boxPlaces.size()))
        {
            break;                                      // 位置已满
        }

        // 把当前新数字写入下一个 box_place。
        state_.boxPlaces[nextBoxIndex_] = digit;        // 写入该帧级多数顺序中的数字
        nextBoxIndex_++;                                // 下一个写入位置
        result.addedCount++;                            // 新增计数 +1
    }

    // 四个候选排序写完后，推断数字只能写到最后一个物理位置place5，不能参与X排序。
    state_.boxPlaces[nextBoxIndex_] = inferredPlace5Digit;
    nextBoxIndex_++;
    result.addedCount++;
    std::cout << "[数字推断] place5不可见，15-前四个数字之和="
              << inferredPlace5Digit << "，已写入place5" << std::endl;

    // 五个位置都写满，箱子区识别完成。
    state_.boxReady = nextBoxIndex_ >= static_cast<int>(state_.boxPlaces.size());

    // 提交后打印当前正式缓存，现场可以直接核对是否按物理画面从右到左排列。
    std::cout << "[数字缓存] 已保存=" << nextBoxIndex_ << "/5，当前数组=[";
    for (size_t i = 0; i < state_.boxPlaces.size(); ++i)
    {
        if (i != 0) std::cout << ' ';
        std::cout << state_.boxPlaces[i];
    }
    std::cout << ']';
    std::cout << std::endl;

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
