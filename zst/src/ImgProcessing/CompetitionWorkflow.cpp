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
// A组不会调用本函数，仍由FieldStateCollector按X坐标从右到左保存全部豆子。
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

    // 构造时先建立空内存。比赛默认每次启动自动删除上场断点，避免忘按R后
    // 从旧阶段继续；只有明确关闭clearProgressOnStart时才允许尝试恢复。
    resetMemory();
    bool restored = false;
    if (config_.clearProgressOnStart)
    {
        std::cout << "[断点管理] 启动策略=自动清理，不加载上场比赛进度" << std::endl;
        clearProgressFile();
    }
    else
    {
        std::cout << "[断点管理] 启动策略=允许恢复，将检查断点文件" << std::endl;
        restored = loadProgress();
    }

    if (!restored)
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
    // TeamA使用完整3豆排列一致性投票；TeamB仍按中心单豆类别投票。
    FieldStateCollectorConfig collectorConfig;
    collectorConfig.voteFramesPerAngle = std::max(1, config_.voteFramesPerStage);   // 至少 1 帧
    collectorConfig.minHitsPerAngle = std::max(1, config_.minHitsPerStage);         // 至少 1 次
    collectorConfig.minConsistentOrderFrames =
        std::clamp(config_.minConsistentOrderFrames, 1,
                   collectorConfig.voteFramesPerAngle);
    collectorConfig.selectMostFrequentBeanOnly =
        config_.mode == TeamMode::TeamB;                    // B组重复中心豆子不会改选旁边候选
    return FieldStateCollector(collectorConfig);
}

void CompetitionWorkflow::resetMemory()
{
    // 重新构造而不是只清数组，是为了让切换TeamA/TeamB时，单次新增限制和
    // “只选择最高票中心豆子”配置都随队伍重新建立。
    collector_ = makeCollector();                           // 按当前模式重建收集器
    teamAStage_ = TeamAStage::WaitingBeans;                 // TeamA 新流程从3个豆子阶段开始
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

    int ignoredNextSequence = 1;

    // version 1/2是旧A组“先数字后豆子”流程；version 3是A组最后合并发送6字节；
    // version 4开始在豆子、数字识别完成后各发送一次3字节结果。B组结构没有改变。
    bool parsed = readExpectedKey(input, "version") && static_cast<bool>(input >> version)
               && readExpectedKey(input, "mode") && static_cast<bool>(input >> savedMode)
               && readExpectedKey(input, "stage") && static_cast<bool>(input >> savedStage);
    if (parsed && version == 2)
        parsed = readExpectedKey(input, "next_sequence")
              && static_cast<bool>(input >> ignoredNextSequence);
    parsed = parsed && (version == 1 || version == 2 || version == 3 || version == 4)
          && readExpectedKey(input, "beans")
          && static_cast<bool>(input >> beanCodes[0] >> beanCodes[1] >> beanCodes[2])
          && readExpectedKey(input, "boxes")
          && static_cast<bool>(input >> savedState.boxPlaces[0]
                                           >> savedState.boxPlaces[1]
                                           >> savedState.boxPlaces[2]
                                           >> savedState.boxPlaces[3]
                                           >> savedState.boxPlaces[4]);

    if (!parsed || (version == 2 && (ignoredNextSequence < 1 || ignoredNextSequence > 255)))
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

    // A组旧断点无法证明豆子位置消息是否已发送；继续恢复可能让电控漏掉第一帧。
    // 因此A组只恢复version 4，B组旧断点仍可正常恢复。
    if (config_.mode == TeamMode::TeamA && version < 4)
    {
        std::cout << "[WorkflowResume] 检测到旧A组断点版本，因两阶段发送协议已改变，"
                     "本次从3个豆子阶段重新开始" << std::endl;
        resetMemory();
        return false;
    }

    for (int i = 0; i < 3; ++i)
        savedState.beanPlaces[static_cast<size_t>(i)] = beanFromCode(beanCodes[i]);

    collector_.restoreState(savedState);                  // 同时重建去重表和写入下标

    bool stageValid = false;
    if (config_.mode == TeamMode::TeamA)
    {
        if (savedStage == "waiting_beans")
        {
            teamAStage_ = TeamAStage::WaitingBeans;
            stageValid = collector_.boxCount() == 0 && collector_.beanCount() == 0;
        }
        else if (savedStage == "waiting_digits")
        {
            teamAStage_ = TeamAStage::WaitingDigits;
            stageValid = collector_.beanReady() && collector_.boxCount() == 0;
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
            stageValid = collector_.beanCount() >= 1 && collector_.boxCount() == 0;
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
        stage = teamAStage_ == TeamAStage::WaitingBeans ? "waiting_beans"
              : teamAStage_ == TeamAStage::WaitingDigits ? "waiting_digits"
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
    output << "version 4\n";
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
    namespace fs = std::filesystem;
    if (config_.progressFile.empty())
    {
        std::cout << "[断点管理] progress_file为空，无需清理" << std::endl;
        return;
    }

    // saveProgress采用“先写.tmp，再替换正式文件”。异常断电可能留下临时文件，
    // 新比赛启动时两者都删除，确保目录中没有任何上场未完成数据。
    const fs::path target(config_.progressFile);
    fs::path temporary = target;
    temporary += ".tmp";

    bool removedAnything = false;
    bool removalFailed = false;
    for (const fs::path &path : {target, temporary})
    {
        std::error_code ec;
        const bool removed = fs::remove(path, ec);
        if (ec)
        {
            removalFailed = true;
            std::cerr << "[断点管理] 删除失败: " << path.string()
                      << "，原因=" << ec.message()
                      << "；本次仍从空内存开始且不会加载该文件" << std::endl;
        }
        else if (removed)
        {
            removedAnything = true;
            std::cout << "[断点管理] 已自动删除旧文件: " << path.string()
                      << std::endl;
        }
    }

    if (!removedAnything && !removalFailed)
        std::cout << "[断点管理] 未发现旧断点，本场直接从头开始" << std::endl;
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

VisionTxPacket CompetitionWorkflow::emitPacket(VisionMessageType type,
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

    VisionTxPacket packet = buildWorkflowPacket(type, data);

    // 无ACK模式下立即推进，避免下一帧重复生成同一阶段结果。
    if (!advanceStageAfterSend(type))
        std::cerr << "[Workflow] 当前阶段与待发送CMD不一致，未推进流程" << std::endl;
    else if (!saveProgress())
        std::cerr << "[WorkflowResume] 结果已生成，但断点保存失败" << std::endl;

    return packet;
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

bool CompetitionWorkflow::advanceStageAfterSend(VisionMessageType command)
{
    if (config_.mode == TeamMode::TeamA)
    {
        if (teamAStage_ == TeamAStage::WaitingBeans &&
            command == VisionMessageType::TeamABeanPositions)
        {
            teamAStage_ = TeamAStage::WaitingDigits;
            std::cout << "[A组状态] 豆子位置已生成并准备发送，下一步：识别数字位置"
                      << std::endl;
            return true;
        }
        if (teamAStage_ == TeamAStage::WaitingDigits &&
            command == VisionMessageType::TeamADigitPositions)
        {
            teamAStage_ = TeamAStage::Finished;
            std::cout << "[A组状态] 数字位置已生成并准备发送，A组视觉流程完成"
                      << std::endl;
            return true;
        }
        return false;
    }

    if (teamBStage_ == TeamBStage::WaitingFirstBean &&
        command == VisionMessageType::BeanCode)
    {
        teamBStage_ = TeamBStage::WaitingDigits;
        std::cout << "[B组状态] 第一个豆子已生成并准备发送，下一步：识别5个数字位置"
                  << std::endl;
        return true;
    }
    if (teamBStage_ == TeamBStage::WaitingDigits &&
        command == VisionMessageType::DigitLayout)
    {
        teamBStage_ = TeamBStage::WaitingRemainingBeans;
        std::cout << "[B组状态] 数字数组已生成并准备发送，下一步：识别中心位置的新豆子"
                  << std::endl;
        return true;
    }
    if (teamBStage_ == TeamBStage::WaitingRemainingBeans &&
        command == VisionMessageType::BeanCode)
    {
        if (collector_.beanReady())
        {
            teamBStage_ = TeamBStage::Finished;
            std::cout << "[B组状态] 第三个豆子已生成并准备发送，B组视觉流程完成"
                      << std::endl;
        }
        else
        {
            std::cout << "[B组状态] 当前豆子已生成并准备发送，继续等待中心位置的未识别豆子"
                      << std::endl;
        }
        return true;
    }
    return false;
}

std::vector<VisionTxPacket> CompetitionWorkflow::updateTeamA(
    const std::vector<Detection> &detections)
{
    std::vector<VisionTxPacket> packets;

    if (teamAStage_ == TeamAStage::WaitingBeans)
    {
        // A组第一阶段只收集豆子。完整3目标排列通过多帧一致性投票后整批保存。
        // 因而beanPlaces[0..2]代表物理位置1~3，而不是固定代表黄、绿、白。
        collector_.addBeanFrame(detections);                // 累计豆子帧；中间投票不刷屏

        if (collector_.beanReady())
        {
            std::cout << "[A组识别] 3个豆子全部识别完成" << std::endl;
            logTeamABeanPositions();                       // 固定按黄、绿、白输出各自位置
            // 第一次发送固定3字节：[黄豆位置, 绿豆位置, 白芸豆位置]。
            packets.push_back(emitPacket(VisionMessageType::TeamABeanPositions,
                                          teamABeanPositionsData()));
        }
        return packets;
    }

    if (teamAStage_ == TeamAStage::WaitingDigits)
    {
        // 第二阶段只收集数字箱。boxPlaces[0..4]依次代表从右到左的place1~place5，
        // 数组值是各物理位置上识别到的数字。
        collector_.addBoxFrame(detections);                 // 累计数字箱帧；中间投票不刷屏

        if (collector_.boxReady())
        {
            std::cout << "[A组识别] 5个箱位数字全部识别完成" << std::endl;
            logDigitLayout();                              // 显示完整5个箱位，便于现场核对
            logTeamADigitPositions();                      // 显示第二次发送的3字节业务含义

            // 第二次发送固定3字节：[数字1位置, 数字2位置, 数字3位置]。
            // 数字4和5仍用于确认布局完整，但不进入A组DATA。
            packets.push_back(emitPacket(VisionMessageType::TeamADigitPositions,
                                          teamADigitPositionsData()));
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
            packets.push_back(emitPacket(VisionMessageType::BeanCode,
                                          beanCodeData(before)));
        }
        return packets;
    }

    if (teamBStage_ == TeamBStage::WaitingDigits)
    {
        // 数字按画面从右到左写入place1~place5，数组值不做反向换算。
        collector_.addBoxFrame(detections);                 // 中间投票不输出，完成时统一显示
        if (collector_.boxReady())
        {
            std::cout << "[B组识别] 5个数字位置全部识别完成" << std::endl;
            logDigitLayout();                              // 只在5个箱位全部稳定后输出一次
            // 此时数字位置已知，补充显示第一个豆子对应数字位于第几个位置。
            logBeanResult(0, "第一个豆子位置匹配");
            packets.push_back(emitPacket(VisionMessageType::DigitLayout, digitsData()));
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
            packets.push_back(emitPacket(VisionMessageType::BeanCode,
                                          beanCodeData(before)));
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

std::vector<uint8_t> CompetitionWorkflow::teamABeanPositionsData() const
{
    return {
        beanPosition(BeanType::Soybean),                    // DATA[0]：黄豆在第几个位置
        beanPosition(BeanType::MungBean),                   // DATA[1]：绿豆在第几个位置
        beanPosition(BeanType::WhiteKidneyBean)             // DATA[2]：白芸豆在第几个位置
    };
}

std::vector<uint8_t> CompetitionWorkflow::teamADigitPositionsData() const
{
    return {
        digitPosition(1),                                   // DATA[0]：数字1所在箱位
        digitPosition(2),                                   // DATA[1]：数字2所在箱位
        digitPosition(3)                                    // DATA[2]：数字3所在箱位
    };
}

std::vector<uint8_t> CompetitionWorkflow::beanCodeData(int beanIndex) const
{
    // B组只需要发送豆子类型码本身，不发送豆子序号和匹配关系。
    if (beanIndex < 0 || beanIndex >= 3) return {0};
    return {encodeBeanType(collector_.state().beanPlaces[beanIndex])};
}

uint8_t CompetitionWorkflow::beanPosition(BeanType bean) const
{
    // 例如beanPlaces={绿豆,黄豆,白芸豆}，查询结果依次为：
    // 黄豆位置=2、绿豆位置=1、白芸豆位置=3。
    if (bean == BeanType::Unknown) return 0;
    const auto &beans = collector_.state().beanPlaces;
    for (size_t i = 0; i < beans.size(); ++i)
        if (beans[i] == bean) return static_cast<uint8_t>(i + 1);
    return 0;                                               // 未识别完整时返回0作为无效位置
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

void CompetitionWorkflow::logTeamABeanPositions() const
{
    const int soybean = static_cast<int>(beanPosition(BeanType::Soybean));
    const int mungBean = static_cast<int>(beanPosition(BeanType::MungBean));
    const int whiteKidney = static_cast<int>(beanPosition(BeanType::WhiteKidneyBean));

    std::cout << "[A组豆子位置] 按黄豆/绿豆/白芸豆固定顺序 = ["
              << soybean << ' ' << mungBean << ' ' << whiteKidney << "]" << std::endl;
}

void CompetitionWorkflow::logTeamADigitPositions() const
{
    const std::vector<uint8_t> data = teamADigitPositionsData();
    std::cout << "[A组数字位置] 按数字1/2/3固定顺序 = ["
              << static_cast<int>(data[0]) << ' '
              << static_cast<int>(data[1]) << ' '
              << static_cast<int>(data[2]) << ']' << std::endl;
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
