#ifndef COMPETITION_WORKFLOW_H
#define COMPETITION_WORKFLOW_H

#include "Communication/VisionProtocol.h"
#include "ImgProcessing/FieldStateCollector.h"
#include <string>
#include <vector>

struct CompetitionWorkflowConfig
{
    TeamMode mode = TeamMode::TeamA;        // 当前队伍模式
    int voteFramesPerStage = 20;            // 每阶段累计投票帧数
    int minHitsPerStage = 6;                // 稳定结果最少命中帧数
    float teamBCenterWidthRatio = 0.40f;    // B组中心豆子区域宽度比例

    // 无ACK模式下，稳定结果生成并准备发送后立即推进阶段并保存断点。
    // 这只能保证视觉端重启后续跑，不能证明电控已经实际收到数据。
    bool resumeProgress = true;
    std::string progressFile = "runtime/workflow_progress.txt";
};

class CompetitionWorkflow
{
public:
    explicit CompetitionWorkflow(const CompetitionWorkflowConfig &config);

    // 每处理完一帧调用一次；稳定结果出现时返回一条待发送消息。
    std::vector<VisionTxPacket> update(const std::vector<Detection> &detections,
                                       int imageWidth);

    void switchMode(TeamMode mode); // 切队并清空状态
    void reset();                   // 新比赛清空内存和磁盘断点

    TeamMode mode() const;
    bool finished() const;
    const FieldState &state() const;

private:
    enum class TeamAStage
    {
        WaitingBeans,
        WaitingDigits,
        Finished
    };

    enum class TeamBStage
    {
        WaitingFirstBean,
        WaitingDigits,
        WaitingRemainingBeans,
        Finished
    };

    FieldStateCollector makeCollector() const;
    void resetMemory();
    bool loadProgress();
    bool saveProgress() const;
    void clearProgressFile() const;

    // 建立无SEQ结果包，并立即推进到下一识别阶段、保存断点。
    VisionTxPacket emitPacket(VisionMessageType type,
                              const std::vector<uint8_t> &data);
    bool advanceStageAfterSend(VisionMessageType type);

    std::vector<VisionTxPacket> updateTeamA(const std::vector<Detection> &detections);
    std::vector<VisionTxPacket> updateTeamB(const std::vector<Detection> &detections,
                                            int imageWidth);

    std::vector<uint8_t> digitsData() const;
    std::vector<uint8_t> teamAResultData() const;
    std::vector<uint8_t> beanCodeData(int beanIndex) const;

    uint8_t beanPosition(BeanType bean) const;
    uint8_t digitPosition(int digit) const;
    void logDigitLayout() const;
    void logTeamABeanPositions() const;
    void logTeamAFinalResult() const;
    void logBeanResult(int beanIndex, const char *stage) const;

    CompetitionWorkflowConfig config_;
    FieldStateCollector collector_;
    TeamAStage teamAStage_ = TeamAStage::WaitingBeans;
    TeamBStage teamBStage_ = TeamBStage::WaitingFirstBean;
};

#endif // COMPETITION_WORKFLOW_H
