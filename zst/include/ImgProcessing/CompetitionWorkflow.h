#ifndef COMPETITION_WORKFLOW_H
#define COMPETITION_WORKFLOW_H

#include "Communication/VisionProtocol.h"
#include "ImgProcessing/FieldStateCollector.h"
#include <cstdint>
#include <vector>

struct CompetitionWorkflowConfig
{
    // 两个队伍不同时间上场，运行前通过 YAML 选择；调试窗口也可按 1/2 切换。
    TeamMode mode = TeamMode::TeamA;        // 当前队伍模式，默认队伍A

    // 每个观察阶段的多帧投票参数。
    int voteFramesPerStage = 20;            // 每个阶段累计多少帧后投票
    int minHitsPerStage = 6;                // 每个阶段最少命中帧数阈值

    // 仅用于B组豆子阶段：允许参加投票的中心区域宽度占整幅图像的比例。
    // 默认0.40表示只接受画面横向30%~70%范围内的豆子；A组完全不使用此参数。
    float teamBCenterWidthRatio = 0.40f;

};

class CompetitionWorkflow
{
public:
    // 构造时会根据 mode 创建对应的收集器并进入该队伍的第一个阶段。
    // 注意：这里只保存和推进比赛流程，不负责相机取图、YOLO推理或真正写串口。
    explicit CompetitionWorkflow(const CompetitionWorkflowConfig &config);

    // 每完成一帧 YOLO/SVM 识别后调用一次。
    // 输入 detections：当前帧已经完成坐标还原和 NMS 的豆子/数字箱结果。
    // 输入 imageWidth：当前原图宽度，仅供B组计算画面中心；A组不会使用。
    // 返回值：本帧新产生的零个或多个串口消息；返回空数组表示尚未满足阶段完成条件。
    //
    // 返回vector保留了后续一次生成多条消息的扩展能力；当前每个阶段最多产生一帧。
    std::vector<VisionTxPacket> update(const std::vector<Detection> &detections,
                                       int imageWidth);

    // 切换队伍会清空所有识别缓存，防止两队结果混用。
    // 如果目标 mode 与当前一致，则什么也不做，避免误按按键导致识别进度被清空。
    void switchMode(TeamMode mode);

    // 在当前队伍模式下重新开始：清空豆子/数字、投票桶和完成状态。
    void reset();

    TeamMode mode() const;                  // 获取当前队伍模式
    bool finished() const;                  // 当前队伍流程是否已完成
    const FieldState &state() const;       // 获取当前已保存的整场状态

private:
    // 队伍A的流程非常线性：先收齐五个数字，再收齐三个豆子，最后结束。
    enum class TeamAStage
    {
        WaitingDigits, // 当前帧只统计数字箱，豆子检测结果被忽略
        WaitingBeans,  // 当前帧只统计豆子，数字箱检测结果被忽略
        Finished       // 所需消息已经生成，后续 update() 直接返回空数组
    };

    // 队伍B的豆子类型顺序不固定：中心是什么就记录什么，但同一类型只记录一次。
    enum class TeamBStage
    {
        WaitingFirstBean,      // 中心第一个新豆子稳定后立即发送其类型码
        WaitingDigits,         // 五个位置数字齐全后发送 DigitLayout
        WaitingRemainingBeans, // 中心每出现一个未记录的新豆子就发送其类型码
        Finished               // 三种豆子均已识别并发送
    };

    // 根据队伍模式创建投票收集器。TeamB 会限制每次提交最多保存一个新豆子。
    FieldStateCollector makeCollector() const;

    // 将业务DATA与CMD组合为最简payload：[CMD][DATA...]。
    VisionTxPacket makePacket(VisionMessageType type, const std::vector<uint8_t> &data);

    std::vector<VisionTxPacket> updateTeamA(const std::vector<Detection> &detections);
    std::vector<VisionTxPacket> updateTeamB(const std::vector<Detection> &detections,
                                            int imageWidth);

    // 下列函数只负责把业务对象转换成 DATA 字节，不添加 A6、协议字段或 CRC。
    std::vector<uint8_t> digitsData() const;                // 5字节：boxA~boxE的数字
    std::vector<uint8_t> beansData() const;                 // 3字节：bean1~bean3的类型
    std::vector<uint8_t> beanCodeData(int beanIndex) const;     // 1字节：豆子类型码1/2/3

    // 在 boxPlaces 中查找某个数字位于哪个物理箱位；返回1~5，没找到返回0。
    uint8_t digitPosition(int digit) const;

    // 下列函数只输出便于现场核对的中文日志，不改变状态，也不发送额外串口消息。
    void logDigitLayout() const;
    void logBeanResult(int beanIndex, const char *stage) const;

    CompetitionWorkflowConfig config_; // 当前队伍、投票参数和B组中心区域
    FieldStateCollector collector_;    // 跨帧投票及最终 FieldState
    TeamAStage teamAStage_ = TeamAStage::WaitingDigits;
    TeamBStage teamBStage_ = TeamBStage::WaitingFirstBean;
};

#endif // COMPETITION_WORKFLOW_H
