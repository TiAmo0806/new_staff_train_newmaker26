#include "ImgProcessing/FieldStateCollector.h"
#include <algorithm>
#include <iostream>

namespace
{
// 计算检测框中心点的 x 坐标。
// 当前角度内需要按”从左到右”保存，所以只需要比较 x。
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
    seenDigits_ = {};

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
    for (int digit : savedState.boxPlaces)
    {
        if (digit < 1 || digit > 5 || seenDigits_[digit]) continue;
        if (nextBoxIndex_ >= static_cast<int>(state_.boxPlaces.size())) break;

        state_.boxPlaces[nextBoxIndex_] = digit;
        seenDigits_[digit] = true;
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
    boxStats_ = {};                                     // 清空投票桶
    for (int i = 0; i < static_cast<int>(boxStats_.size()); ++i)
    {
        boxStats_[i].id = i;                            // 还原候选编号（下标即编号）
    }
}

AngleCommitResult FieldStateCollector::addBeanFrame(const std::vector<Detection> &detections)
{
    // 三个豆子位置已经收集完成后，后续豆子帧直接忽略。
    if (state_.beanReady)
    {
        return {};                                      // 已完成，无需再累加
    }

    // 同一帧可能产生同类别重复框。和数字逻辑一样，每个豆子类别只选择置信度
    // 最高的一个框参与本帧计票，防止单帧重复框把命中数和平均X坐标放大。
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

    for (int code = 1; code <= 3; ++code)
    {
        const Detection *best = bestDetectionByBean[code];
        if (best == nullptr) continue;                   // 本帧没有识别到该豆子类型

        beanStats_[code].count++;                        // 每个类别每帧最多命中一次
        beanStats_[code].xSum += detectionCenterX(*best); // 累加最佳框中心X
    }

    // 当前角度累计一帧。
    beanFrameCount_++;                                  // 当前角度帧数 +1

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

    // 同一帧中，同一个数字可能因为模型重复框而出现多次。
    // 先为每个数字只选择置信度最高的一个框，保证“命中次数”真正表示命中了多少帧，
    // 而不是某一帧产生了多少重复框；平均X坐标也不会被重复框带偏。
    std::array<const Detection *, 6> bestDetectionByDigit{};
    for (const auto &d : detections)
    {
        // 箱子阶段只看数字箱框，豆子框全部跳过。
        if (d.kind != TargetKind::DigitBox || d.digit < 1 || d.digit > 5)
        {
            continue;                                   // 跳过非数字箱检测或无效数字
        }

        // 已经由前一个角度保存过的数字不再参与本角度投票，避免重叠画面影响新数字排序。
        if (seenDigits_[d.digit]) continue;

        const Detection *&best = bestDetectionByDigit[d.digit];
        if (best == nullptr || d.score > best->score)
            best = &d;                                  // 同数字重复框只留下置信度最高者
    }

    for (int digit = 1; digit <= 5; ++digit)
    {
        const Detection *best = bestDetectionByDigit[digit];
        if (best == nullptr) continue;                   // 本帧没有可靠看到这个数字

        boxStats_[digit].count++;                        // 每个数字在一帧中最多命中一次
        boxStats_[digit].xSum += detectionCenterX(*best); // 累加该帧最佳框中心X
    }

    // 当前角度累计一帧。
    boxFrameCount_++;                                   // 当前角度帧数 +1

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

    const bool teamBMode = config_.selectMostFrequentBeanOnly;
    const char *logPrefix = teamBMode ? "[B组中心豆子核对]" : "[A组豆子核对]";

    // 先筛出当前角度内”出现次数足够多”的豆子。
    // A组只考虑尚未保存的新类别；B组必须保留已经保存的最高票类别，
    // 这样中心出现重复豆子时会明确判重，而不会错误改选旁边/第二名类别。
    std::vector<CandidateStat> candidates;
    for (int id = 1; id <= 3; ++id)
    {
        if (beanStats_[id].count >= config_.minHitsPerAngle &&
            (teamBMode || !seenBeans_[id]))
        {
            candidates.push_back(beanStats_[id]);       // 命中次数达标，加入候选
        }
    }

    // 每轮投票完成都打印识别候选，A/B两组现场都能看到模型稳定识别了什么。
    std::cout << logPrefix << " 稳定候选=";
    if (candidates.empty())
    {
        std::cout << "无";
    }
    else
    {
        for (std::size_t i = 0; i < candidates.size(); ++i)
        {
            if (i != 0) std::cout << " | ";
            std::cout << beanChineseName(candidates[i].id)
                      << "(命中=" << candidates[i].count
                      << ",平均X=" << candidates[i].averageX() << ')';
        }
    }
    std::cout << std::endl;

    if (teamBMode)
    {
        // B组输入已经被上层筛成“每帧唯一中心豆子”。若20帧内类别有抖动，
        // 选择出现次数最多的类别，而不是按X选择。只保留第一名非常重要：
        // 如果第一名是已经记忆过的豆子，本轮应判定为重复，不能改选第二名并误发。
        std::sort(candidates.begin(), candidates.end(), [](const CandidateStat &a,
                                                           const CandidateStat &b) {
            if (a.count != b.count) return a.count > b.count;
            return a.id < b.id; // 次数相同时固定按编码打破平局，保证结果可重复
        });
        // 第一名并列说明中心目标在本轮不稳定，宁可继续观察，也不猜测并误发。
        if (candidates.size() > 1 && candidates[0].count == candidates[1].count)
        {
            std::cout << "[B组中心豆子核对] 最高票并列，中心类别不稳定，本轮不保存"
                      << std::endl;
            candidates.clear();
        }
        else if (candidates.size() > 1)
            candidates.resize(1);

        if (!candidates.empty())
        {
            std::cout << "[B组中心豆子识别] 最终确认="
                      << beanChineseName(candidates[0].id)
                      << "，命中=" << candidates[0].count
                      << "，平均X=" << candidates[0].averageX() << std::endl;
        }
    }
    else
    {
        // A组必须先凑齐本阶段全部新豆子，再允许写入任何一个位置。
        // 例如只稳定识别到黄豆和白芸豆时，本轮两个都不保存，下一轮重新完整投票，
        // 从根本上避免跨轮追加破坏三者的物理左右顺序。
        const int remainingCapacity =
            static_cast<int>(state_.beanPlaces.size()) - nextBeanIndex_;
        const int requiredCount =
            std::min(std::max(1, config_.minNewBeansPerCommit), remainingCapacity);
        if (static_cast<int>(candidates.size()) < requiredCount)
        {
            std::cout << "[A组豆子核对] 稳定新豆子=" << candidates.size()
                      << '/' << requiredCount
                      << "，尚未全部识别，本轮一个也不保存" << std::endl;
            resetBeanAngle();
            return result;
        }

        // 三类全部稳定后，按多帧平均X从左到右一次性确定物理位置1~3。
        std::sort(candidates.begin(), candidates.end(), [](const CandidateStat &a,
                                                           const CandidateStat &b) {
            return a.averageX() < b.averageX();
        });
        std::cout << "[A组豆子排序] 三类均已稳定，从左到右=";
        for (std::size_t i = 0; i < candidates.size(); ++i)
        {
            if (i != 0) std::cout << " -> ";
            std::cout << beanChineseName(candidates[i].id)
                      << "(命中=" << candidates[i].count
                      << ",平均X=" << candidates[i].averageX() << ')';
        }
        std::cout << std::endl;
    }

    for (const auto &candidate : candidates)
    {
        // 队伍B要求一次只确认一个新豆子。限制在这里实现后，
        // 即使同一画面同时出现多个豆子，也只保存排序后的第一个新类别；
        // 其余候选不会丢失为永久结果，而是在下一个观察阶段重新累计20帧再判断。
        if (config_.maxNewBeansPerCommit > 0 &&
            result.addedCount >= config_.maxNewBeansPerCommit)
        {
            break;                                      // 达到单次提交上限
        }

        // 如果这种豆子之前已经保存过，说明这是多角度重叠识别，跳过。
        if (seenBeans_[candidate.id])
        {
            if (teamBMode)
                std::cout << "[B组豆子保存] " << beanChineseName(candidate.id)
                          << "以前已经保存，本轮不重复保存、不重复发送" << std::endl;
            continue;                                   // 已保存过的豆子类型
        }

        // 三个豆子位置已经写满，不再保存。
        if (nextBeanIndex_ >= static_cast<int>(state_.beanPlaces.size()))
        {
            break;                                      // 位置已满
        }

        // 把当前新豆子写入下一个 bean_place。
        state_.beanPlaces[nextBeanIndex_] = decodeBeanType(candidate.id); // 写入豆子类型
        seenBeans_[candidate.id] = true;                // 标记为已见过
        nextBeanIndex_++;                               // 下一个写入位置
        result.addedCount++;                            // 新增计数 +1
    }

    // 三个位置都写满，豆子区识别完成。
    state_.beanReady = nextBeanIndex_ >= static_cast<int>(state_.beanPlaces.size());

    // 保存日志与识别候选日志分开：前者明确表示哪些结果已经进入正式缓存，
    // 后续状态机和串口只会使用这里展示的正式缓存。
    if (result.addedCount > 0)
    {
        std::cout << (teamBMode ? "[B组豆子保存]" : "[A组豆子保存]")
                  << " 新增=" << result.addedCount << "，正式缓存=[";
        for (int i = 0; i < nextBeanIndex_; ++i)
        {
            if (i != 0) std::cout << " | ";
            const int code = static_cast<int>(encodeBeanType(state_.beanPlaces[i]));
            std::cout << "位置" << (i + 1) << '=' << beanChineseName(code)
                      << "(编码" << code << ')';
        }
        std::cout << "]，完成=" << (state_.beanReady ? "是" : "否") << std::endl;
    }
    else if (candidates.empty())
    {
        std::cout << logPrefix << " 本轮没有可保存结果，继续采集下一轮" << std::endl;
    }

    // 一个角度提交完后，清空临时投票，准备下一个角度。
    resetBeanAngle();
    return result;
}

AngleCommitResult FieldStateCollector::commitBoxAngle()
{
    AngleCommitResult result;
    result.committed = true;                            // 标记本次调用完成了一轮角度核对

    // 先筛出当前角度内”出现次数足够多”的数字。
    // 出现次数太少的数字可能是误检，不保存。
    std::vector<CandidateStat> candidates;
    for (int digit = 1; digit <= 5; ++digit)
    {
        if (!seenDigits_[digit] &&
            boxStats_[digit].count >= config_.minHitsPerAngle)
        {
            candidates.push_back(boxStats_[digit]);     // 未保存且命中次数达标，加入候选
        }
    }

    // 普通多角度模式下，第一角度通常要求4个新数字；数组只剩1个空位时，
    // requiredCount会自动降为1。place5推断模式则固定要求第一轮恰好4个。
    const int totalCapacity = static_cast<int>(state_.boxPlaces.size());
    const int remainingCapacity = totalCapacity - nextBoxIndex_;
    const bool inferPlace5ThisRound =
        config_.inferPlace5FromFirstFour && nextBoxIndex_ == 0 && remainingCapacity == 5;
    const int requiredCount = inferPlace5ThisRound
        ? 4
        : std::min(std::max(1, config_.minNewDigitsPerCommit), remainingCapacity);

    // 当前角度内部按画面从左到右排序。
    // 例如当前角度看到 1-3-4，就按 1、3、4 的顺序处理。
    std::sort(candidates.begin(), candidates.end(), [](const CandidateStat &a, const CandidateStat &b) {
        return a.averageX() < b.averageX();             // 按平均 x 升序 = 从左到右
    });

    // 推断模式必须“恰好4个”：少于4个无法推断，出现5个则与“画面只含place1~4”矛盾，
    // 很可能混入误检，因此两种情况都整批拒绝。普通模式只要求不少于requiredCount。
    const bool candidateCountInvalid = inferPlace5ThisRound
        ? static_cast<int>(candidates.size()) != requiredCount
        : static_cast<int>(candidates.size()) < requiredCount;
    if (candidateCountInvalid)
    {
        std::cout << "[数字角度核对] 稳定新数字=" << candidates.size()
                  << "，要求=" << (inferPlace5ThisRound ? "恰好" : "至少")
                  << requiredCount << "，本轮不保存；候选=";
        if (candidates.empty())
        {
            std::cout << "无";
        }
        else
        {
            for (size_t i = 0; i < candidates.size(); ++i)
            {
                if (i != 0) std::cout << ", ";
                std::cout << candidates[i].id
                          << "(命中=" << candidates[i].count
                          << ",平均X=" << candidates[i].averageX() << ')';
            }
        }
        std::cout << std::endl;
        resetBoxAngle();
        return result;
    }

    // 数字1~5各出现一次，总和固定为15。推断模式下四个候选已经保证互不重复，
    // 因而15减去四个候选之和就是唯一缺失数字，也就是不可见的place5内容。
    int inferredPlace5Digit = 0;
    if (inferPlace5ThisRound)
    {
        int detectedDigitSum = 0;
        for (const CandidateStat &candidate : candidates)
            detectedDigitSum += candidate.id;
        inferredPlace5Digit = 15 - detectedDigitSum;

        const bool inferredDigitInvalid =
            inferredPlace5Digit < 1 || inferredPlace5Digit > 5 ||
            std::any_of(candidates.begin(), candidates.end(), [&](const CandidateStat &candidate) {
                return candidate.id == inferredPlace5Digit;
            });
        if (inferredDigitInvalid)
        {
            // 理论上“4个互不重复且均在1~5”不会进入这里；保留防御检查，避免未来改动
            // 放宽候选条件后，把0、负数或重复数字写入正式数组。
            std::cerr << "[数字角度核对] place5推断非法：四个数字总和="
                      << detectedDigitSum << "，推断值=" << inferredPlace5Digit
                      << "，本轮不保存" << std::endl;
            resetBoxAngle();
            return result;
        }
    }

    // 数量达标后再统一输出排序核对信息。这里的顺序就是即将写入boxPlaces的顺序。
    std::cout << "[数字角度核对] 数量达标，从左到右=";
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (i != 0) std::cout << " -> ";
        std::cout << candidates[i].id
                  << "(命中=" << candidates[i].count
                  << ",平均X=" << candidates[i].averageX() << ')';
    }
    std::cout << std::endl;

    for (const auto &candidate : candidates)
    {
        // 如果这个数字之前已经保存过，说明它是重叠区域重复看到的数字，跳过。
        if (seenDigits_[candidate.id])
        {
            continue;                                   // 已保存过的数字
        }

        // 五个箱子位置已经写满，不再保存。
        if (nextBoxIndex_ >= static_cast<int>(state_.boxPlaces.size()))
        {
            break;                                      // 位置已满
        }

        // 把当前新数字写入下一个 box_place。
        state_.boxPlaces[nextBoxIndex_] = candidate.id; // 写入数字
        seenDigits_[candidate.id] = true;               // 标记为已见过
        nextBoxIndex_++;                                // 下一个写入位置
        result.addedCount++;                            // 新增计数 +1
    }

    if (inferPlace5ThisRound)
    {
        // 当前相机画面固定从左到右对应place1~place4，所以四个候选排序写完后，
        // 推断数字只能写到最后一个物理位置place5，不能参与X排序。
        state_.boxPlaces[nextBoxIndex_] = inferredPlace5Digit;
        seenDigits_[inferredPlace5Digit] = true;
        nextBoxIndex_++;
        result.addedCount++;
        std::cout << "[数字推断] place5不可见，15-前四个数字之和="
                  << inferredPlace5Digit << "，已写入place5" << std::endl;
    }

    // 五个位置都写满，箱子区识别完成。
    state_.boxReady = nextBoxIndex_ >= static_cast<int>(state_.boxPlaces.size());

    // 提交后打印当前正式缓存，现场可以直接核对是否按物理画面从左到右排列。
    std::cout << "[数字缓存] 已保存=" << nextBoxIndex_ << "/5，当前数组=[";
    for (size_t i = 0; i < state_.boxPlaces.size(); ++i)
    {
        if (i != 0) std::cout << ' ';
        std::cout << state_.boxPlaces[i];
    }
    std::cout << ']';
    if (!state_.boxReady)
        std::cout << "，请切换角度识别剩余数字";
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
