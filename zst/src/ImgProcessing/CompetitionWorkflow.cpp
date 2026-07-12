#include "ImgProcessing/CompetitionWorkflow.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace
{
// 协议把 session=0 视为无效/未初始化，因此外部即使配置0，也会修正为1。
uint8_t safeSession(uint8_t value)
{
    return value == 0 ? 1 : value;      // 0 被修正为 1，其余不变
}

// B组从所有豆子中选择“位于中心区域、距离画面中心最近”的唯一目标。
// 不预设黄豆/绿豆/白芸豆顺序，中心是什么就返回什么；是否已经识别过由收集器判断。
// 其他豆子不送进FieldStateCollector，因此不会保存、不会标记为已识别，也不会影响后续阶段。
// A组不会调用本函数，仍由FieldStateCollector按X坐标从左到右保存全部豆子。
std::vector<Detection> centerBean(const std::vector<Detection> &detections,
                                  int imageWidth,
                                  float centerWidthRatio)
{
    if (imageWidth <= 0) return {};

    const float imageCenterX = static_cast<float>(imageWidth) * 0.5f;
    const float safeRatio = std::clamp(centerWidthRatio, 0.05f, 1.0f);
    const float halfAllowedWidth = static_cast<float>(imageWidth) * safeRatio * 0.5f;

    const Detection *best = nullptr;
    float bestDistance = std::numeric_limits<float>::max();
    for (const Detection &d : detections)
    {
        if (d.kind != TargetKind::Bean) continue;

        const float beanCenterX = static_cast<float>(d.box.x) +
                                  static_cast<float>(d.box.width) * 0.5f;
        const float distance = std::abs(beanCenterX - imageCenterX);
        if (distance > halfAllowedWidth) continue; // 中心区域之外的豆子完全忽略

        // 距离相同时选择置信度更高的框，保证结果稳定且可预测。
        if (best == nullptr || distance < bestDistance ||
            (distance == bestDistance && d.score > best->score))
        {
            best = &d;
            bestDistance = distance;
        }
    }

    if (best == nullptr) return {};
    return {*best};
}

const char *beanChineseName(BeanType bean)
{
    switch (bean)
    {
    case BeanType::Soybean: return "黄豆";
    case BeanType::MungBean: return "绿豆";
    case BeanType::WhiteKidneyBean: return "白芸豆";
    default: return "未知豆子";
    }
}
}

CompetitionWorkflow::CompetitionWorkflow(const CompetitionWorkflowConfig &config)
    : config_(config), collector_(makeCollector())
{
    // 初始化列表先按传入配置创建 collector_；随后规范化 session 并统一调用 reset，
    // 让所有状态、投票桶和序号都从确定值开始。
    config_.sessionId = safeSession(config_.sessionId);     // 确保 session 不为 0
    config_.teamBCenterWidthRatio =
        std::clamp(config_.teamBCenterWidthRatio, 0.05f, 1.0f);
    reset();
}

FieldStateCollector CompetitionWorkflow::makeCollector() const
{
    // FieldStateCollector 原本可以在一次角度提交中保存多个新豆子。
    // TeamA正好需要一次看到多个豆子；TeamB要求"识别一个、发送一个"，
    // 所以TeamB把 maxNewBeansPerCommit 限制为1。
    FieldStateCollectorConfig collectorConfig;
    collectorConfig.voteFramesPerAngle = std::max(1, config_.voteFramesPerStage);   // 至少 1 帧
    collectorConfig.minHitsPerAngle = std::max(1, config_.minHitsPerStage);         // 至少 1 次
    collectorConfig.maxNewBeansPerCommit =
        config_.mode == TeamMode::TeamB ? 1 : 3;            // TeamB 每次最多新增 1 个豆子
    collectorConfig.selectMostFrequentBeanOnly =
        config_.mode == TeamMode::TeamB;                    // B组重复中心豆子不会改选旁边候选
    return FieldStateCollector(collectorConfig);
}

void CompetitionWorkflow::reset()
{
    // 重新构造而不是只清数组，是为了让切换TeamA/TeamB时，
    // maxNewBeansPerCommit 也能随模式从3变1或从1变3。
    collector_ = makeCollector();                           // 按当前模式重建收集器
    teamAStage_ = TeamAStage::WaitingDigits;                // TeamA 从数字阶段开始
    teamBStage_ = TeamBStage::WaitingFirstBean;             // TeamB 从任意中心豆子开始
    nextSequence_ = 1;                                      // 序号从 1 开始
    std::cout << "[Workflow] reset, mode=" << teamModeToString(config_.mode)
              << ", session=" << static_cast<int>(config_.sessionId);
    if (config_.mode == TeamMode::TeamB)
        std::cout << ", B组中心区域宽度="
                  << (config_.teamBCenterWidthRatio * 100.0f) << "%";
    std::cout << std::endl;
}

void CompetitionWorkflow::switchMode(TeamMode mode)
{
    // 同模式重复选择不重置，防止调试时重复按键丢失已经完成的投票。
    if (config_.mode == mode) return;                       // 模式未变，直接返回

    // 新队伍使用新session。C板即使残留上一场数据，也可以通过session区分。
    config_.mode = mode;                                    // 更新队伍模式
    config_.sessionId = safeSession(static_cast<uint8_t>(config_.sessionId + 1)); // session 递增
    reset();                                                // 重建所有状态
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
    // sequence表示"本次会话的第几条业务消息"。它不是识别数量，也不是帧号。
    const uint8_t sequence = nextSequence_++;               // 取当前序号，然后递增
    if (nextSequence_ == 0) nextSequence_ = 1;              // 序号溢出后回到 1

    std::cout << "[发送准备] team=" << teamModeToString(config_.mode)
              << ", type=" << visionMessageTypeToString(type)
              << ", session=" << static_cast<int>(config_.sessionId)
              << ", seq=" << static_cast<int>(sequence)
              << ", data=[";
    for (size_t i = 0; i < data.size(); ++i)
    {
        if (i != 0) std::cout << ' ';
        std::cout << static_cast<int>(data[i]);
    }
    std::cout << "]" << std::endl;
    return buildWorkflowPacket(config_.mode, type, config_.sessionId, sequence, data);
}

std::vector<VisionTxPacket> CompetitionWorkflow::update(
    const std::vector<Detection> &detections,
    int imageWidth)
{
    // 流程结束后不再累计投票，也不重复发送完成消息。
    if (finished()) return {};                              // 已完成，直接返回空

    // 队伍差异全部收口在这里；main、YOLO和串口层不需要散布team判断。
    return config_.mode == TeamMode::TeamA
        ? updateTeamA(detections)                           // TeamA 流程
        : updateTeamB(detections, imageWidth);              // TeamB 中心豆子流程
}

std::vector<VisionTxPacket> CompetitionWorkflow::updateTeamA(
    const std::vector<Detection> &detections)
{
    std::vector<VisionTxPacket> packets;

    if (teamAStage_ == TeamAStage::WaitingDigits)
    {
        // 本阶段只把 DigitBox 检测交给数字投票器。
        // 即使移动过程中画面仍出现豆子，也不会污染豆子结果。
        const AngleCommitResult commit = collector_.addBoxFrame(detections); // 累计数字箱帧
        if (commit.committed)
            std::cout << "[Workflow:A] digits " << collector_.boxCount() << "/5" << std::endl;

        if (collector_.boxReady())
        {
            // DATA为5字节：依次表示物理箱位A~E上识别到的数字。
            // 生成消息后立即进入豆子阶段；单向版本不等待电控ACK。
            packets.push_back(makePacket(VisionMessageType::DigitsComplete, digitsData()));
            teamAStage_ = TeamAStage::WaitingBeans;         // 进入豆子阶段
        }
        return packets;
    }

    if (teamAStage_ == TeamAStage::WaitingBeans)
    {
        // 数字结果继续保存在同一个FieldState中，本阶段只新增豆子结果。
        const AngleCommitResult commit = collector_.addBeanFrame(detections); // 累计豆子帧
        if (commit.committed)
            std::cout << "[Workflow:A] beans " << collector_.beanCount() << "/3" << std::endl;

        if (collector_.beanReady())
        {
            // 先发送"豆子阶段完成"，让C板知道可以进入下一动作；
            // 再发送完整结果。两者是两帧独立消息，TYPE和SEQUENCE均不同。
            packets.push_back(makePacket(VisionMessageType::BeansComplete, beansData()));
            packets.push_back(makePacket(VisionMessageType::FinalResult, finalResultData()));
            teamAStage_ = TeamAStage::Finished;             // TeamA 流程结束
        }
    }
    return packets;
}

std::vector<VisionTxPacket> CompetitionWorkflow::updateTeamB(
    const std::vector<Detection> &detections,
    int imageWidth)
{
    std::vector<VisionTxPacket> packets;

    if (teamBStage_ == TeamBStage::WaitingFirstBean)
    {
        // 顺序不固定：每帧只把最靠近画面中心的那个豆子交给投票器。
        // 豆子稳定后保存其类型并发送对应码：黄豆1、绿豆2、白芸豆3。
        const int before = collector_.beanCount();
        const AngleCommitResult commit = collector_.addBeanFrame(
            centerBean(detections, imageWidth, config_.teamBCenterWidthRatio));
        if (commit.committed)
            std::cout << "[B组投票] 第一个中心豆子阶段提交，新增="
                      << commit.addedCount << std::endl;
        if (collector_.beanCount() > before)
        {
            logBeanResult(before, "第一个中心豆子识别完成");
            packets.push_back(makePacket(VisionMessageType::BeanCode,
                                         beanCodeData(before)));
            teamBStage_ = TeamBStage::WaitingDigits;
            std::cout << "[B组状态] 第一个豆子已记忆并发送，下一步：识别5个数字位置"
                      << std::endl;
        }
        return packets;
    }

    if (teamBStage_ == TeamBStage::WaitingDigits)
    {
        // 数字按画面从左到右写入place1~place5，数组值不做反向换算。
        const AngleCommitResult commit = collector_.addBoxFrame(detections);
        if (commit.committed)
        {
            std::cout << "[B组投票] 数字阶段提交，新增=" << commit.addedCount
                      << "，当前已记录=" << collector_.boxCount() << "/5" << std::endl;
            logDigitLayout();
        }
        if (collector_.boxReady())
        {
            std::cout << "[B组识别] 5个数字位置全部识别完成" << std::endl;
            // 此时数字位置已知，补充显示第一个豆子对应数字位于第几个位置。
            logBeanResult(0, "第一个豆子位置匹配");
            packets.push_back(makePacket(VisionMessageType::DigitLayout, digitsData()));
            teamBStage_ = TeamBStage::WaitingRemainingBeans;
            std::cout << "[B组状态] 数字数组已发送，下一步：继续识别中心位置的新豆子"
                      << std::endl;
        }
        return packets;
    }

    if (teamBStage_ == TeamBStage::WaitingRemainingBeans)
    {
        // 仍然只看画面中心。FieldStateCollector会记住已经保存过的豆子类型：
        // 已识别类型再次处于中心时，本轮addedCount为0，不重复保存、不重复发送。
        const int before = collector_.beanCount();
        const AngleCommitResult commit = collector_.addBeanFrame(
            centerBean(detections, imageWidth, config_.teamBCenterWidthRatio));
        if (commit.committed)
            std::cout << "[B组投票] 后续中心豆子阶段提交，新增=" << commit.addedCount
                      << "，已记忆=" << collector_.beanCount() << "/3" << std::endl;
        if (collector_.beanCount() > before)
        {
            logBeanResult(before, "新的中心豆子识别完成");
            packets.push_back(makePacket(VisionMessageType::BeanCode,
                                         beanCodeData(before)));
            if (collector_.beanReady())
            {
                teamBStage_ = TeamBStage::Finished;
                std::cout << "[B组状态] 三种豆子均已记忆并发送，B组视觉流程完成"
                          << std::endl;
            }
            else
            {
                std::cout << "[B组状态] 新豆子已记忆并发送，继续等待中心位置的未识别豆子"
                          << std::endl;
            }
        }
        return packets;
    }
    return packets;
}

std::vector<uint8_t> CompetitionWorkflow::digitsData() const
{
    // boxPlaces下标代表固定物理位置A~E，数组值代表该位置上的数字。
    // 非法值统一发0，避免C板把未完成数据当成有效目标。
    std::vector<uint8_t> data;
    data.reserve(5);                                        // 5 个箱位
    for (int digit : collector_.state().boxPlaces)
        data.push_back(digit >= 1 && digit <= 5 ? static_cast<uint8_t>(digit) : 0); // 有效数字或 0
    return data;
}

std::vector<uint8_t> CompetitionWorkflow::beansData() const
{
    // beanPlaces下标代表第1~3个豆子位置，encodeBeanType负责业务枚举转线路编码。
    std::vector<uint8_t> data;
    data.reserve(3);                                        // 3 个豆子位置
    for (BeanType bean : collector_.state().beanPlaces)
        data.push_back(encodeBeanType(bean));               // 枚举 -> uint8_t 编码
    return data;
}

std::vector<uint8_t> CompetitionWorkflow::beanCodeData(int beanIndex) const
{
    // B组只需要发送豆子类型码本身，不发送豆子序号和匹配关系。
    if (beanIndex < 0 || beanIndex >= 3) return {0};
    return {encodeBeanType(collector_.state().beanPlaces[beanIndex])};
}

uint8_t CompetitionWorkflow::digitPosition(int digit) const
{
    // 例如boxPlaces={4,1,3,2,5}，查找数字1会返回箱位2。
    if (digit < 1 || digit > 5) return 0;                   // 无效数字
    const auto &boxes = collector_.state().boxPlaces;
    for (size_t i = 0; i < boxes.size(); ++i)
        if (boxes[i] == digit) return static_cast<uint8_t>(i + 1); // 找到数字所在箱位
    return 0;                                               // 未找到
}

void CompetitionWorkflow::logDigitLayout() const
{
    const auto &places = collector_.state().boxPlaces;
    std::cout << "[数字位置] [place1=" << places[0]
              << ", place2=" << places[1]
              << ", place3=" << places[2]
              << ", place4=" << places[3]
              << ", place5=" << places[4] << "]" << std::endl;
}

void CompetitionWorkflow::logBeanResult(int beanIndex, const char *stage) const
{
    if (beanIndex < 0 || beanIndex >= 3) return;
    const BeanType bean = collector_.state().beanPlaces[beanIndex];
    const int beanCode = static_cast<int>(encodeBeanType(bean));
    const int targetDigit = targetDigitForBean(bean);
    const int place = static_cast<int>(digitPosition(targetDigit));

    std::cout << "[豆子结果] " << stage
              << "：豆子=" << beanChineseName(bean)
              << "，发送码=" << beanCode
              << "，对应数字=" << targetDigit;
    if (place == 0)
        std::cout << "，数字位置=尚未识别";
    else
        std::cout << "，数字位于第" << place << "个位置(place" << place << ")";
    std::cout << std::endl;
}

std::vector<uint8_t> CompetitionWorkflow::finalResultData() const
{
    // DATA布局：
    //   [0..2]  三个豆子位置的豆子类型
    //   [3..7]  五个固定箱位A~E上的数字
    //   [8..10] bean1~bean3各自目标数字所在的物理箱位
    std::vector<uint8_t> data = beansData();                // 先放 3 字节豆子数据
    const std::vector<uint8_t> digits = digitsData();
    data.insert(data.end(), digits.begin(), digits.end());  // 追加 5 字节数字数据

    // 最后三字节分别表示 bean1/2/3 对应数字所在的箱位。
    for (BeanType bean : collector_.state().beanPlaces)
        data.push_back(digitPosition(targetDigitForBean(bean))); // 每个豆子的目标箱位
    return data;
}
