#include "ImgProcessing/CompetitionWorkflow.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>

namespace
{
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

// 断点文件使用线路豆子编码，恢复时转回业务枚举。
BeanType beanFromCode(int code)
{
    if (code == 1) return BeanType::Soybean;
    if (code == 2) return BeanType::MungBean;
    if (code == 3) return BeanType::WhiteKidneyBean;
    return BeanType::Unknown;
}

// 简单文本断点格式使用“字段名 值”，读取时逐项校验字段名，防止错位解析。
bool readExpectedKey(std::istream &input, const char *expected)
{
    std::string actual;
    return static_cast<bool>(input >> actual) && actual == expected;
}
}

CompetitionWorkflow::CompetitionWorkflow(const CompetitionWorkflowConfig &config)
    : config_(config), collector_(makeCollector())
{
    // 初始化列表先按传入配置创建collector_，随后规范化中心区域。
    config_.teamBCenterWidthRatio =
        std::clamp(config_.teamBCenterWidthRatio, 0.05f, 1.0f);

    // 构造时不能调用公开reset()，因为reset()会删除磁盘断点。
    // 先初始化干净内存，再尝试恢复上次“已经成功发送”的比赛进度。
    resetMemory();
    if (!loadProgress())
    {
        std::cout << "[Workflow] 从头开始, mode=" << teamModeToString(config_.mode);
        if (config_.mode == TeamMode::TeamB)
            std::cout << ", B组中心区域宽度="
                      << (config_.teamBCenterWidthRatio * 100.0f) << "%";
        std::cout << std::endl;
    }
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

void CompetitionWorkflow::resetMemory()
{
    // 重新构造而不是只清数组，是为了让切换TeamA/TeamB时，单次新增限制和
    // “只选择最高票中心豆子”配置都随队伍重新建立。
    collector_ = makeCollector();                           // 按当前模式重建收集器
    teamAStage_ = TeamAStage::WaitingDigits;                // TeamA 从数字阶段开始
    teamBStage_ = TeamBStage::WaitingFirstBean;             // TeamB 从任意中心豆子开始
}

void CompetitionWorkflow::reset()
{
    resetMemory();                                          // 清除内存结果和临时投票
    clearProgressFile();                                    // 清除跨进程断点
    std::cout << "[Workflow] 已清空进度并重新开始, mode="
              << teamModeToString(config_.mode);
    if (config_.mode == TeamMode::TeamB)
        std::cout << ", B组中心区域宽度="
                  << (config_.teamBCenterWidthRatio * 100.0f) << "%";
    std::cout << std::endl;
}

bool CompetitionWorkflow::loadProgress()
{
    namespace fs = std::filesystem;

    if (!config_.resumeProgress || config_.progressFile.empty()) return false;

    std::error_code ec;
    if (!fs::is_regular_file(config_.progressFile, ec)) return false;

    std::ifstream input(config_.progressFile);
    if (!input)
    {
        std::cerr << "[WorkflowResume] 无法打开进度文件，将从头开始: "
                  << config_.progressFile << std::endl;
        return false;
    }

    int version = 0;
    std::string savedMode;
    std::string savedStage;
    FieldState savedState;
    int beanCodes[3] = {0, 0, 0};

    // 固定顺序文本格式：
    // version 1 / mode team_a / stage waiting_beans /
    // beans b1 b2 b3 / boxes d1 d2 d3 d4 d5
    bool parsed = readExpectedKey(input, "version") && static_cast<bool>(input >> version)
               && readExpectedKey(input, "mode") && static_cast<bool>(input >> savedMode)
               && readExpectedKey(input, "stage") && static_cast<bool>(input >> savedStage)
               && readExpectedKey(input, "beans")
               && static_cast<bool>(input >> beanCodes[0] >> beanCodes[1] >> beanCodes[2])
               && readExpectedKey(input, "boxes")
               && static_cast<bool>(input >> savedState.boxPlaces[0]
                                                >> savedState.boxPlaces[1]
                                                >> savedState.boxPlaces[2]
                                                >> savedState.boxPlaces[3]
                                                >> savedState.boxPlaces[4]);

    if (!parsed || version != 1)
    {
        std::cerr << "[WorkflowResume] 进度文件格式错误，将从头开始: "
                  << config_.progressFile << std::endl;
        return false;
    }

    const std::string currentMode = teamModeToString(config_.mode);
    if (savedMode != currentMode)
    {
        // A/B使用不同C板，切队时绝不能恢复另一队状态。
        std::cout << "[WorkflowResume] 进度属于" << savedMode
                  << "，当前为" << currentMode << "，忽略旧进度" << std::endl;
        return false;
    }

    for (int i = 0; i < 3; ++i)
        savedState.beanPlaces[static_cast<size_t>(i)] = beanFromCode(beanCodes[i]);

    collector_.restoreState(savedState);                  // 同时重建去重表和写入下标

    bool stageValid = false;
    if (config_.mode == TeamMode::TeamA)
    {
        if (savedStage == "waiting_digits")
        {
            teamAStage_ = TeamAStage::WaitingDigits;
            stageValid = collector_.boxCount() == 0 && collector_.beanCount() == 0;
        }
        else if (savedStage == "waiting_beans")
        {
            teamAStage_ = TeamAStage::WaitingBeans;
            stageValid = collector_.boxReady();
        }
        else if (savedStage == "finished")
        {
            teamAStage_ = TeamAStage::Finished;
            stageValid = collector_.boxReady() && collector_.beanReady();
        }
    }
    else
    {
        if (savedStage == "waiting_first_bean")
        {
            teamBStage_ = TeamBStage::WaitingFirstBean;
            stageValid = collector_.beanCount() == 0 && collector_.boxCount() == 0;
        }
        else if (savedStage == "waiting_digits")
        {
            teamBStage_ = TeamBStage::WaitingDigits;
            stageValid = collector_.beanCount() >= 1;
        }
        else if (savedStage == "waiting_remaining_beans")
        {
            teamBStage_ = TeamBStage::WaitingRemainingBeans;
            stageValid = collector_.beanCount() >= 1 && collector_.boxReady();
        }
        else if (savedStage == "finished")
        {
            teamBStage_ = TeamBStage::Finished;
            stageValid = collector_.beanReady() && collector_.boxReady();
        }
    }

    if (!stageValid)
    {
        // 阶段与数组内容不一致时宁可从头识别，也不能带着错误状态跳过比赛步骤。
        std::cerr << "[WorkflowResume] 阶段与缓存内容不一致，将从头开始" << std::endl;
        resetMemory();
        return false;
    }

    std::cout << "[WorkflowResume] 已恢复: mode=" << savedMode
              << "，stage=" << savedStage
              << "，豆子=" << collector_.beanCount() << "/3"
              << "，箱位=" << collector_.boxCount() << "/5"
              << "，文件=" << config_.progressFile << std::endl;
    return true;
}

bool CompetitionWorkflow::saveProgress() const
{
    namespace fs = std::filesystem;

    if (!config_.resumeProgress || config_.progressFile.empty()) return true;

    const fs::path target(config_.progressFile);
    std::error_code ec;
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    if (ec)
    {
        std::cerr << "[WorkflowResume] 无法创建进度目录: "
                  << target.parent_path().string() << std::endl;
        return false;
    }

    fs::path temporary = target;
    temporary += ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output)
    {
        std::cerr << "[WorkflowResume] 无法写入临时进度文件: "
                  << temporary.string() << std::endl;
        return false;
    }

    std::string stage;
    if (config_.mode == TeamMode::TeamA)
    {
        stage = teamAStage_ == TeamAStage::WaitingDigits ? "waiting_digits"
              : teamAStage_ == TeamAStage::WaitingBeans ? "waiting_beans"
                                                        : "finished";
    }
    else
    {
        stage = teamBStage_ == TeamBStage::WaitingFirstBean ? "waiting_first_bean"
              : teamBStage_ == TeamBStage::WaitingDigits ? "waiting_digits"
              : teamBStage_ == TeamBStage::WaitingRemainingBeans ? "waiting_remaining_beans"
                                                                  : "finished";
    }

    const FieldState &current = collector_.state();
    output << "version 1\n";
    output << "mode " << teamModeToString(config_.mode) << "\n";
    output << "stage " << stage << "\n";
    output << "beans "
           << static_cast<int>(encodeBeanType(current.beanPlaces[0])) << ' '
           << static_cast<int>(encodeBeanType(current.beanPlaces[1])) << ' '
           << static_cast<int>(encodeBeanType(current.beanPlaces[2])) << "\n";
    output << "boxes "
           << current.boxPlaces[0] << ' ' << current.boxPlaces[1] << ' '
           << current.boxPlaces[2] << ' ' << current.boxPlaces[3] << ' '
           << current.boxPlaces[4] << "\n";
    output.flush();
    if (!output)
    {
        std::cerr << "[WorkflowResume] 写入进度文件失败" << std::endl;
        return false;
    }
    output.close();

    // 先写.tmp再替换正式文件，避免程序在写到一半时退出留下半个断点文件。
    fs::rename(temporary, target, ec);
    if (ec)
    {
        ec.clear();
        fs::remove(target, ec);                            // Windows已有目标时先删除
        ec.clear();
        fs::rename(temporary, target, ec);
    }
    if (ec)
    {
        std::cerr << "[WorkflowResume] 无法替换正式进度文件: "
                  << target.string() << std::endl;
        return false;
    }

    std::cout << "[WorkflowResume] 已保存断点: stage=" << stage
              << "，文件=" << target.string() << std::endl;
    return true;
}

void CompetitionWorkflow::clearProgressFile() const
{
    if (config_.progressFile.empty()) return;
    std::error_code ec;
    const bool removed = std::filesystem::remove(config_.progressFile, ec);
    if (ec)
        std::cerr << "[WorkflowResume] 删除进度文件失败: "
                  << config_.progressFile << std::endl;
    else if (removed)
        std::cout << "[WorkflowResume] 已删除旧进度: "
                  << config_.progressFile << std::endl;
}

void CompetitionWorkflow::confirmPacketSent(const VisionTxPacket &packet)
{
    // 空payload不可能是合法业务消息；不为异常数据建立断点。
    if (packet.payload.empty()) return;
    if (!saveProgress())
        std::cerr << "[WorkflowResume] 串口已发送，但断点保存失败；退出程序后可能无法续跑"
                  << std::endl;
}

void CompetitionWorkflow::switchMode(TeamMode mode)
{
    // 同模式重复选择不重置，防止调试时重复按键丢失已经完成的投票。
    if (config_.mode == mode) return;                       // 模式未变，直接返回

    config_.mode = mode;                                    // 更新队伍模式
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
    std::cout << "[发送准备] mode=" << teamModeToString(config_.mode)
              << ", cmd=" << visionMessageTypeToString(type)
              << ", data=[";
    for (size_t i = 0; i < data.size(); ++i)
    {
        if (i != 0) std::cout << ' ';
        std::cout << static_cast<int>(data[i]);
    }
    std::cout << "]" << std::endl;
    return buildWorkflowPacket(type, data);
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
        collector_.addBoxFrame(detections);                 // 累计数字箱帧；中间投票不刷屏

        if (collector_.boxReady())
        {
            std::cout << "[A组识别] 5个箱位数字全部识别完成" << std::endl;
            logDigitLayout();                              // 输出place1~place5的完整数字布局
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
        collector_.addBeanFrame(detections);                // 累计豆子帧；中间投票不刷屏

        if (collector_.beanReady())
        {
            std::cout << "[A组识别] 3个豆子全部识别完成" << std::endl;
            // 数字布局已经保存在同一个FieldState中，因此此处可以直接输出
            // 每个豆子的名称、线路编码、对应数字以及该数字所在的物理箱位。
            for (int beanIndex = 0; beanIndex < 3; ++beanIndex)
                logBeanResult(beanIndex, "A组豆子匹配");
            // A组只发送三个豆子的识别数组，不再发送包含target_place的FinalResult。
            packets.push_back(makePacket(VisionMessageType::BeansComplete, beansData()));
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
        collector_.addBeanFrame(
            centerBean(detections, imageWidth, config_.teamBCenterWidthRatio));
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
        collector_.addBoxFrame(detections);                 // 中间投票不输出，完成时统一显示
        if (collector_.boxReady())
        {
            std::cout << "[B组识别] 5个数字位置全部识别完成" << std::endl;
            logDigitLayout();                              // 只在5个箱位全部稳定后输出一次
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
        collector_.addBeanFrame(
            centerBean(detections, imageWidth, config_.teamBCenterWidthRatio));
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
