#ifndef COMPETITION_WORKFLOW_H
#define COMPETITION_WORKFLOW_H

#include "/home/zst/zst/include/Communication/VisionProtocol.h"
#include "/home/zst/zst/include/ImgProcessing/FieldStateCollector.h"
#include <cstdint>
#include <vector>

struct CompetitionWorkflowConfig
{
    // 两个队伍不同时间上场，运行前通过 YAML 选择；调试窗口也可按 1/2 切换。
    TeamMode mode = TeamMode::TeamA;

    // 每个观察阶段的多帧投票参数。
    int voteFramesPerStage = 20;
    int minHitsPerStage = 6;

    // 会话号用于区分“上一次任务”和“本次任务”。切换队伍时会自动递增。
    uint8_t sessionId = 1;
};

class CompetitionWorkflow
{
public:
    // 构造时会根据 mode 创建对应的收集器并进入该队伍的第一个阶段。
    // 注意：这里只保存和推进比赛流程，不负责相机取图、YOLO推理或真正写串口。
    explicit CompetitionWorkflow(const CompetitionWorkflowConfig &config);

    // 每完成一帧 YOLO/SVM 识别后调用一次。
    // 输入 detections：当前帧已经完成坐标还原和 NMS 的豆子/数字箱结果。
    // 返回值：本帧新产生的零个或多个串口消息；返回空数组表示尚未满足阶段完成条件。
    //
    // 为什么返回 vector：队伍A完成豆子阶段时，需要连续产生 BeansComplete 和
    // FinalResult 两个独立消息，因此单帧可能返回多个 VisionTxPacket。
    std::vector<VisionTxPacket> update(const std::vector<Detection> &detections);

    // 切换队伍会清空所有识别缓存并开始新会话，防止两队结果混用。
    // 如果目标 mode 与当前一致，则什么也不做，避免误按按键导致识别进度被清空。
    void switchMode(TeamMode mode);

    // 在当前队伍模式下重新开始：清空豆子/数字、投票桶、消息序号和完成状态。
    // sessionId 不在这里递增；只有 switchMode() 才把它加一。
    void reset();

    TeamMode mode() const;
    bool finished() const;
    const FieldState &state() const;

private:
    // 队伍A的流程非常线性：先收齐五个数字，再收齐三个豆子，最后结束。
    enum class TeamAStage
    {
        WaitingDigits, // 当前帧只统计数字箱，豆子检测结果被忽略
        WaitingBeans,  // 当前帧只统计豆子，数字箱检测结果被忽略
        Finished       // 所需消息已经生成，后续 update() 直接返回空数组
    };

    // 队伍B先单独识别第一个豆子，再扫描全部数字，最后逐个识别剩余豆子。
    enum class TeamBStage
    {
        WaitingFirstBean,      // 收到一个稳定豆子后发送 BeanDetected
        WaitingDigits,         // 五个数字齐全后发送第一个豆子的 BeanDigitMatch
        WaitingRemainingBeans, // 每新增一个稳定豆子立即发送一次 BeanDigitMatch
        Finished               // 三个豆子都已完成匹配
    };

    // 根据队伍模式创建投票收集器。TeamB 会限制每次提交最多保存一个新豆子。
    FieldStateCollector makeCollector() const;

    // 给业务 DATA 添加协议头字段：版本、队伍、消息类型、session、sequence、长度。
    VisionTxPacket makePacket(VisionMessageType type, const std::vector<uint8_t> &data);

    std::vector<VisionTxPacket> updateTeamA(const std::vector<Detection> &detections);
    std::vector<VisionTxPacket> updateTeamB(const std::vector<Detection> &detections);

    // 下列函数只负责把业务对象转换成 DATA 字节，不添加 A6、协议字段或 CRC。
    std::vector<uint8_t> digitsData() const;                // 5字节：boxA~boxE的数字
    std::vector<uint8_t> beansData() const;                 // 3字节：bean1~bean3的类型
    std::vector<uint8_t> beanDetectedData(int beanIndex) const; // 2字节：豆子位置、类型
    std::vector<uint8_t> beanMatchData(int beanIndex) const;    // 4字节：位置、类型、数字、箱位
    std::vector<uint8_t> finalResultData() const;           // 11字节：3豆+5数字+3匹配箱位

    // 在 boxPlaces 中查找某个数字位于哪个物理箱位；返回1~5，没找到返回0。
    uint8_t digitPosition(int digit) const;

    CompetitionWorkflowConfig config_; // 当前队伍、投票参数和会话号
    FieldStateCollector collector_;    // 跨帧投票及最终 FieldState
    TeamAStage teamAStage_ = TeamAStage::WaitingDigits;
    TeamBStage teamBStage_ = TeamBStage::WaitingFirstBean;
    // 每生成一条消息加一。0保留不用，255之后重新回到1。
    // 单向版本暂不等待ACK，但序号便于C板区分新消息和后续扩展去重。
    uint8_t nextSequence_ = 1;
};

#endif // COMPETITION_WORKFLOW_H
