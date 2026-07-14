#ifndef COMPETITION_WORKFLOW_H
#define COMPETITION_WORKFLOW_H

#include "Communication/VisionProtocol.h"
#include "ImgProcessing/FieldStateCollector.h"
#include <cstdint>
#include <optional>
#include <string>
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

    // true时，只在收到电控的正确ACK后保存稳定结果和已推进阶段。
    // Linux write()成功但ACK未到达时不保存，避免把“写入驱动”误当成“电控已接受”。
    bool resumeProgress = true;

    // 进度文件路径由Utils按zst项目根目录解析成绝对路径。
    // 文件只保存已收到ACK的阶段，不保存尚未完成的临时投票帧。
    std::string progressFile = "runtime/workflow_progress.txt";
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

    // 在当前队伍模式下重新开始：清空豆子/数字、投票桶、阶段和磁盘进度。
    // 调试窗口按R时调用；新一场比赛开始前必须执行一次，避免沿用上一场结果。
    void reset();

    // 处理电控返回的结果ACK。
    // 只有SEQ、原CMD都与当前待确认包一致且status=0时，才会推进A/B阶段并保存断点。
    // 返回true表示本次ACK被接受；错误SEQ、错误CMD、NACK和过期重复ACK均返回false。
    bool confirmPacketAcknowledged(uint8_t sequence,
                                   uint8_t originalCommand,
                                   uint8_t status);

    // WaitingAck期间停止新的识别投票。main通过pendingPacket()取得原包并超时重发，
    // 重发必须使用同一个CMD、SEQ和DATA，不能重新生成序号。
    bool waitingForAck() const;
    const VisionTxPacket *pendingPacket() const;

    TeamMode mode() const;                  // 获取当前队伍模式
    bool finished() const;                  // 当前队伍流程是否已完成
    const FieldState &state() const;       // 获取当前已保存的整场状态

private:
    // 结果发送子状态与A/B业务阶段分开：业务阶段记录“正在识别什么”，
    // deliveryState_=WaitingAck时冻结业务阶段，直到电控确认当前结果。
    enum class ResultDeliveryState
    {
        Idle,       // 当前没有待确认结果，可以继续识别
        WaitingAck  // 已生成结果，等待匹配ACK，期间只允许重发原包
    };

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

    // 只清内存，不删除磁盘文件；构造时先初始化内存，再尝试从进度文件恢复。
    void resetMemory();

    // 断点文件只保存已经收到ACK的稳定状态和下一SEQ。
    // 加载失败/模式不匹配时返回false并从头开始。
    bool loadProgress();
    bool saveProgress() const;
    void clearProgressFile() const;

    // 建立唯一待确认结果：[CMD][SEQ][DATA]，同时进入WaitingAck。
    // 当前已有待确认结果时不会再调用本函数生成第二条消息。
    VisionTxPacket queuePacket(VisionMessageType type, const std::vector<uint8_t> &data);

    // 收到匹配且status=0的ACK后，根据当前队伍和CMD推进业务阶段。
    bool advanceStageAfterAck(uint8_t acknowledgedCommand);

    // 当前ACK完成后生成下一个1~255序号；0始终保留为无效值。
    void advanceSequence();

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
    ResultDeliveryState deliveryState_ = ResultDeliveryState::Idle;
    std::optional<VisionTxPacket> pendingPacket_; // WaitingAck期间保存原包供匹配和重发
    uint8_t nextSequence_ = 1;                    // 下一条新结果使用的SEQ，范围1~255
};

#endif // COMPETITION_WORKFLOW_H
