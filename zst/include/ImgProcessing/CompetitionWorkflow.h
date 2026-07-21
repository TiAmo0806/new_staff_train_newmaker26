#ifndef COMPETITION_WORKFLOW_H
#define COMPETITION_WORKFLOW_H

#include "Communication/VisionProtocol.h"
#include "ImgProcessing/FieldStateCollector.h"
#include <optional>
#include <string>
#include <vector>

struct CompetitionWorkflowConfig
{
    TeamMode mode = TeamMode::TeamA;        // 当前队伍模式
    int voteFramesPerStage = 20;            // 每阶段累计投票帧数
    int minHitsPerStage = 6;                // B组单个中心豆子的最少命中帧数
    int minCompleteBeanOrderFrames = 12;    // A组三豆完整排列的直接保存阈值
    int minInferredBeanOrderFrames = 15;    // A组前两豆排列稳定并推测place3的阈值
    int minConsistentOrderFrames = 15;      // A/B数字完整排列在一轮中至少一致多少帧
    float teamBCenterWidthRatio = 0.40f;    // B组中心豆子区域宽度比例

    // 无ACK模式下，稳定结果只有在本机完整写入串口后才推进阶段并保存断点。
    // 这只能证明Linux写调用完成并支持视觉端续跑，不能证明电控已经解析数据。
    bool resumeProgress = true;

    // true（比赛默认）：程序每次启动都删除上场断点并从空状态开始，避免忘按R。
    // 同一进程内camera_state=0/1关闭和重开相机不会触发清理，当前比赛内存仍保留。
    // 若比赛中途程序崩溃且确实需要恢复，可临时改成false后重启一次。
    bool clearProgressOnStart = true;
    std::string progressFile = "runtime/workflow_progress.txt";
};

class CompetitionWorkflow
{
public:
    explicit CompetitionWorkflow(const CompetitionWorkflowConfig &config);

    // 每处理完一帧调用一次；稳定结果出现时返回一条待发送消息。
    std::vector<VisionTxPacket> update(const std::vector<Detection> &detections,
                                       int imageWidth);

    // main确认整帧已经完整写入Linux串口后调用；此时才推进阶段并保存断点。
    // 当前仍是无ACK协议，这只代表本机写入成功，不代表电控已经确认收到。
    bool confirmSent(const VisionTxPacket &packet);

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

    // 建立无SEQ结果包并保存为待发送状态；不在这里推进识别阶段。
    VisionTxPacket emitPacket(VisionMessageType type,
                              const std::vector<uint8_t> &data);
    bool advanceStageAfterSend(VisionMessageType type);

    std::vector<VisionTxPacket> updateTeamA(const std::vector<Detection> &detections);
    std::vector<VisionTxPacket> updateTeamB(const std::vector<Detection> &detections,
                                            int imageWidth);

    std::vector<uint8_t> digitsData() const;
    std::vector<uint8_t> teamABeanPositionsData() const;
    std::vector<uint8_t> teamADigitPositionsData() const;
    std::vector<uint8_t> beanCodeData(int beanIndex) const;

    uint8_t beanPosition(BeanType bean) const;
    uint8_t digitPosition(int digit) const;
    void logDigitLayout() const;
    void logTeamABeanPositions() const;
    void logTeamADigitPositions() const;
    void logBeanResult(int beanIndex, const char *stage) const;

    CompetitionWorkflowConfig config_;
    FieldStateCollector collector_;
    TeamAStage teamAStage_ = TeamAStage::WaitingBeans;
    TeamBStage teamBStage_ = TeamBStage::WaitingFirstBean;
    std::optional<VisionTxPacket> pendingPacket_; // 写入失败时冻结流程并重试同一业务包
};

#endif // COMPETITION_WORKFLOW_H
