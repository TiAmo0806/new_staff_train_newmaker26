#include "ImgProcessing/CompetitionWorkflow.h"
#include <algorithm>
#include <iostream>

namespace
{
// 协议把 session=0 视为无效/未初始化，因此外部即使配置0，也会修正为1。
uint8_t safeSession(uint8_t value)
{
    return value == 0 ? 1 : value;
}
}

CompetitionWorkflow::CompetitionWorkflow(const CompetitionWorkflowConfig &config)
    : config_(config), collector_(makeCollector())
{
    // 初始化列表先按传入配置创建 collector_；随后规范化 session 并统一调用 reset，
    // 让所有状态、投票桶和序号都从确定值开始。
    config_.sessionId = safeSession(config_.sessionId);
    reset();
}

FieldStateCollector CompetitionWorkflow::makeCollector() const
{
    // FieldStateCollector 原本可以在一次角度提交中保存多个新豆子。
    // TeamA正好需要一次看到多个豆子；TeamB要求“识别一个、发送一个”，
    // 所以TeamB把 maxNewBeansPerCommit 限制为1。
    FieldStateCollectorConfig collectorConfig;
    collectorConfig.voteFramesPerAngle = std::max(1, config_.voteFramesPerStage);
    collectorConfig.minHitsPerAngle = std::max(1, config_.minHitsPerStage);
    collectorConfig.maxNewBeansPerCommit =
        config_.mode == TeamMode::TeamB ? 1 : 3;
    return FieldStateCollector(collectorConfig);
}

void CompetitionWorkflow::reset()
{
    // 重新构造而不是只清数组，是为了让切换TeamA/TeamB时，
    // maxNewBeansPerCommit 也能随模式从3变1或从1变3。
    collector_ = makeCollector();
    teamAStage_ = TeamAStage::WaitingDigits;
    teamBStage_ = TeamBStage::WaitingFirstBean;
    nextSequence_ = 1;
    std::cout << "[Workflow] reset, mode=" << teamModeToString(config_.mode)
              << ", session=" << static_cast<int>(config_.sessionId) << std::endl;
}

void CompetitionWorkflow::switchMode(TeamMode mode)
{
    // 同模式重复选择不重置，防止调试时重复按键丢失已经完成的投票。
    if (config_.mode == mode) return;

    // 新队伍使用新session。C板即使残留上一场数据，也可以通过session区分。
    config_.mode = mode;
    config_.sessionId = safeSession(static_cast<uint8_t>(config_.sessionId + 1));
    reset();
}

TeamMode CompetitionWorkflow::mode() const
{
    return config_.mode;
}

bool CompetitionWorkflow::finished() const
{
    return config_.mode == TeamMode::TeamA
        ? teamAStage_ == TeamAStage::Finished
        : teamBStage_ == TeamBStage::Finished;
}

const FieldState &CompetitionWorkflow::state() const
{
    return collector_.state();
}

VisionTxPacket CompetitionWorkflow::makePacket(VisionMessageType type,
                                                const std::vector<uint8_t> &data)
{
    // sequence表示“本次会话的第几条业务消息”。它不是识别数量，也不是帧号。
    const uint8_t sequence = nextSequence_++;
    if (nextSequence_ == 0) nextSequence_ = 1;

    std::cout << "[Workflow] message=" << visionMessageTypeToString(type)
              << ", seq=" << static_cast<int>(sequence)
              << ", data_len=" << data.size() << std::endl;
    return buildWorkflowPacket(config_.mode, type, config_.sessionId, sequence, data);
}

std::vector<VisionTxPacket> CompetitionWorkflow::update(
    const std::vector<Detection> &detections)
{
    // 流程结束后不再累计投票，也不重复发送完成消息。
    if (finished()) return {};

    // 队伍差异全部收口在这里；main、YOLO和串口层不需要散布team判断。
    return config_.mode == TeamMode::TeamA
        ? updateTeamA(detections)
        : updateTeamB(detections);
}

std::vector<VisionTxPacket> CompetitionWorkflow::updateTeamA(
    const std::vector<Detection> &detections)
{
    std::vector<VisionTxPacket> packets;

    if (teamAStage_ == TeamAStage::WaitingDigits)
    {
        // 本阶段只把 DigitBox 检测交给数字投票器。
        // 即使移动过程中画面仍出现豆子，也不会污染豆子结果。
        const AngleCommitResult commit = collector_.addBoxFrame(detections);
        if (commit.committed)
            std::cout << "[Workflow:A] digits " << collector_.boxCount() << "/5" << std::endl;

        if (collector_.boxReady())
        {
            // DATA为5字节：依次表示物理箱位A~E上识别到的数字。
            // 生成消息后立即进入豆子阶段；单向版本不等待电控ACK。
            packets.push_back(makePacket(VisionMessageType::DigitsComplete, digitsData()));
            teamAStage_ = TeamAStage::WaitingBeans;
        }
        return packets;
    }

    if (teamAStage_ == TeamAStage::WaitingBeans)
    {
        // 数字结果继续保存在同一个FieldState中，本阶段只新增豆子结果。
        const AngleCommitResult commit = collector_.addBeanFrame(detections);
        if (commit.committed)
            std::cout << "[Workflow:A] beans " << collector_.beanCount() << "/3" << std::endl;

        if (collector_.beanReady())
        {
            // 先发送“豆子阶段完成”，让C板知道可以进入下一动作；
            // 再发送完整结果。两者是两帧独立消息，TYPE和SEQUENCE均不同。
            packets.push_back(makePacket(VisionMessageType::BeansComplete, beansData()));
            packets.push_back(makePacket(VisionMessageType::FinalResult, finalResultData()));
            teamAStage_ = TeamAStage::Finished;
        }
    }
    return packets;
}

std::vector<VisionTxPacket> CompetitionWorkflow::updateTeamB(
    const std::vector<Detection> &detections)
{
    std::vector<VisionTxPacket> packets;

    if (teamBStage_ == TeamBStage::WaitingFirstBean)
    {
        // before是加入本帧前已经保存的豆子数量。
        // TeamB收集器限制一次最多新增1个，因此新增豆子的下标就是before。
        const int before = collector_.beanCount();
        collector_.addBeanFrame(detections);
        if (collector_.beanCount() > before)
        {
            // BeanDetected只发送豆子位置和豆子类型；此时尚未扫描全部数字，
            // 因而还无法给出对应数字位于哪个箱位。
            packets.push_back(makePacket(VisionMessageType::BeanDetected,
                                         beanDetectedData(before)));
            teamBStage_ = TeamBStage::WaitingDigits;
        }
        return packets;
    }

    if (teamBStage_ == TeamBStage::WaitingDigits)
    {
        // 第一个豆子保存在beanPlaces[0]，现在累计五个数字箱位置。
        collector_.addBoxFrame(detections);
        if (collector_.boxReady())
        {
            // 数字完整结果保存在 collector_ 中；此时直接发送第一个豆子的匹配结果。
            // 例如黄豆对应数字1，再在boxPlaces中查找数字1的物理箱位。
            packets.push_back(makePacket(VisionMessageType::BeanDigitMatch,
                                         beanMatchData(0)));
            teamBStage_ = TeamBStage::WaitingRemainingBeans;
        }
        return packets;
    }

    if (teamBStage_ == TeamBStage::WaitingRemainingBeans)
    {
        // 后续仍复用同一个豆子收集器。seenBeans_会跳过第一个已识别豆子，
        // 每次稳定新增一个豆子，就立即生成该豆子的匹配消息。
        const int before = collector_.beanCount();
        collector_.addBeanFrame(detections);
        if (collector_.beanCount() > before)
        {
            packets.push_back(makePacket(VisionMessageType::BeanDigitMatch,
                                         beanMatchData(before)));
            // beanReady说明三个豆子位置均已填写，第三次匹配消息生成后流程结束。
            if (collector_.beanReady()) teamBStage_ = TeamBStage::Finished;
        }
    }
    return packets;
}

std::vector<uint8_t> CompetitionWorkflow::digitsData() const
{
    // boxPlaces下标代表固定物理位置A~E，数组值代表该位置上的数字。
    // 非法值统一发0，避免C板把未完成数据当成有效目标。
    std::vector<uint8_t> data;
    data.reserve(5);
    for (int digit : collector_.state().boxPlaces)
        data.push_back(digit >= 1 && digit <= 5 ? static_cast<uint8_t>(digit) : 0);
    return data;
}

std::vector<uint8_t> CompetitionWorkflow::beansData() const
{
    // beanPlaces下标代表第1~3个豆子位置，encodeBeanType负责业务枚举转线路编码。
    std::vector<uint8_t> data;
    data.reserve(3);
    for (BeanType bean : collector_.state().beanPlaces)
        data.push_back(encodeBeanType(bean));
    return data;
}

std::vector<uint8_t> CompetitionWorkflow::beanDetectedData(int beanIndex) const
{
    // 线路位置从1开始，而C++数组下标从0开始，因此需要+1。
    if (beanIndex < 0 || beanIndex >= 3) return {0, 0};
    return {
        static_cast<uint8_t>(beanIndex + 1),
        encodeBeanType(collector_.state().beanPlaces[beanIndex])
    };
}

uint8_t CompetitionWorkflow::digitPosition(int digit) const
{
    // 例如boxPlaces={4,1,3,2,5}，查找数字1会返回箱位2。
    if (digit < 1 || digit > 5) return 0;
    const auto &boxes = collector_.state().boxPlaces;
    for (size_t i = 0; i < boxes.size(); ++i)
        if (boxes[i] == digit) return static_cast<uint8_t>(i + 1);
    return 0;
}

std::vector<uint8_t> CompetitionWorkflow::beanMatchData(int beanIndex) const
{
    // 匹配关系由比赛规则决定：黄豆->1、绿豆->2、白芸豆->3。
    // targetDigitForBean只决定“对应哪个数字”；digitPosition再决定该数字实际放在哪里。
    if (beanIndex < 0 || beanIndex >= 3) return {0, 0, 0, 0};
    const BeanType bean = collector_.state().beanPlaces[beanIndex];
    const int targetDigit = targetDigitForBean(bean);
    return {
        static_cast<uint8_t>(beanIndex + 1), // 豆子位置：1~3
        encodeBeanType(bean),               // 豆子类型：1~3
        static_cast<uint8_t>(targetDigit),  // 对应数字：1~3
        digitPosition(targetDigit)           // 对应数字所在箱位：1~5
    };
}

std::vector<uint8_t> CompetitionWorkflow::finalResultData() const
{
    // DATA布局：
    //   [0..2]  三个豆子位置的豆子类型
    //   [3..7]  五个固定箱位A~E上的数字
    //   [8..10] bean1~bean3各自目标数字所在的物理箱位
    std::vector<uint8_t> data = beansData();
    const std::vector<uint8_t> digits = digitsData();
    data.insert(data.end(), digits.begin(), digits.end());

    // 最后三字节分别表示 bean1/2/3 对应数字所在的箱位。
    for (BeanType bean : collector_.state().beanPlaces)
        data.push_back(digitPosition(targetDigitForBean(bean)));
    return data;
}
