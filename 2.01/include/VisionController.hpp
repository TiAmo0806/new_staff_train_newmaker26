/**
 * @file VisionController.hpp
 * @brief 视觉系统状态机 — 全自动"抓豆子-放箱子"比赛控制器
 *
 * 该控制器是实现比赛策略的核心。它管理一个完整的状态机，
 * 协调 YOLO 视觉推理与电控（MCU）串口通信，完成以下流程：
 *
 *   1. 扫描建图（取豆区 3 个位置 + 放置区 5 个箱子）
 *   2. 取放循环（按顺序抓取白芸豆 → 黄豆 → 绿豆）
 *
 * 各阶段子状态:
 *   ┌─ 扫描建图 ─────────────────────────────┐
 *   │  WAIT_START → SCAN_BEAN_AREA →          │
 *   │  WAIT_BOX_SCAN → SCAN_BOX_AREA          │
 *   └─────────────────────────────────────────┘
 *   ┌─ 取放循环 ───────────────────────────────┐
 *   │  IDENTIFY_BEAN → WAIT_POSITION →         │
 *   │  CONFIRM_POSITION → WAIT_ACTION →        │
 *   │  SEARCH_BOX → WAIT_BOX_POSITION →        │
 *   │  CONFIRM_BOX_POSITION → WAIT_ACTION →    │
 *   │  (循环) → IDLE                           │
 *   └─────────────────────────────────────────┘
 *
 * @see 最新的识别策略 文档（项目根目录）了解完整策略说明
 */

#ifndef VISION_CONTROLLER_HPP
#define VISION_CONTROLLER_HPP

#include "RobotVision.hpp"
#include "SerialPort.hpp"
#include <memory>
#include <vector>
#include <string>
#include <atomic>
#include <map>
#include <set>
#include <chrono>

// ============================================
// ⭐ 抓取参数配置（比赛时在这里修改）
// ============================================
#define BEAN_WHITE_KIDNEY_TARGET_COUNT  1   // 白芸豆抓取次数（先抓）
#define BEAN_SOYBEAN_TARGET_COUNT       1   // 黄豆抓取次数（其次）
#define BEAN_MUNG_BEAN_TARGET_COUNT     1   // 绿豆抓取次数（最后）
// ============================================

/**
 * @brief 目标区域类型（视觉→电控 packet 中的 target_type）
 *
 * 告知电控目标在哪个区域，电控根据此信息决定移动方向。
 */
enum TargetType : uint8_t {
    TARGET_NONE = 0,            // 无目标 / 让电控继续转动寻找
    TARGET_BEAN_BOX = 1,        // 目标在取豆区（装豆子的箱子）
    TARGET_BOX = 2,             // 目标在放置区（贴数字的箱子）
};

/**
 * @brief 豆子类别枚举
 *
 * 注意枚举值与 class_id 保持一致：
 *   soybean(0) → 黄豆, mung_bean(1) → 绿豆, white_kidney_bean(2) → 白芸豆
 */
enum BeanClass : int {
    BEAN_SOYBEAN = 0,
    BEAN_MUNG_BEAN = 1,
    BEAN_WHITE_KIDNEY_BEAN = 2
};

/**
 * @brief 状态机状态枚举
 *
 * 扫描建图阶段：
 *   STATE_WAIT_START        等待电控启动信号
 *   STATE_SCAN_BEAN_AREA    扫描取豆区，建立位置→豆子映射表
 *   STATE_WAIT_BOX_SCAN     取豆区扫完，等待电控转动摄像头到放置区
 *   STATE_SCAN_BOX_AREA     扫描放置区，建立位置→箱号映射表
 *
 * 取放循环阶段（每颗豆子一轮）：
 *   STATE_IDENTIFY_BEAN        查映射表 → 发取豆区位置信号给电控
 *   STATE_SEARCH_BOX           查映射表 → 发放置区位置信号给电控
 *   STATE_WAIT_POSITION        等待电控移动到取豆目标位置
 *   STATE_CONFIRM_POSITION     电控已到位，视觉确认取豆目标是否在视野中
 *   STATE_WAIT_BOX_POSITION    等待电控移动到放置目标位置
 *   STATE_CONFIRM_BOX_POSITION 电控已到位，视觉确认放置目标是否在视野中
 *   STATE_WAIT_ACTION          等待电控执行抓取/放置动作
 *   STATE_IDLE                 所有豆子已放完，任务完成
 */
enum RobotState {
    STATE_WAIT_START,               // 等待电控启动信号
    STATE_SCAN_BEAN_AREA,           // 扫描取豆区（建立位置→豆子映射）
    STATE_WAIT_BOX_SCAN,            // ⭐ 取豆区扫完，等待电控转摄像头到放置区
    STATE_SCAN_BOX_AREA,            // 扫描放置区（建立位置→箱号映射）
    STATE_IDENTIFY_BEAN,            // 识别取豆区箱子位置 → 发位置信号给电控
    STATE_SEARCH_BOX,               // 找放置区箱子位置 → 发位置信号给电控
    STATE_WAIT_POSITION,            // ⭐ 等待电控移动到取豆目标位置
    STATE_CONFIRM_POSITION,         // ⭐ 电控已到位，视觉确认取豆目标是否在视野中
    STATE_WAIT_BOX_POSITION,        // ⭐ 等待电控移动到放置目标位置
    STATE_CONFIRM_BOX_POSITION,     // ⭐ 电控已到位，视觉确认放置目标是否在视野中
    STATE_WAIT_ACTION,              // 等待电控执行抓取/放置动作
    STATE_IDLE                      // 任务完成
};

// 豆子 → 目标箱号映射（每种豆子需要放到对应的箱子里）
static const std::map<int, int> BEAN_TO_BOX = {
    {BEAN_SOYBEAN, 1},             // 黄豆 → 1 号箱 (data_1)
    {BEAN_MUNG_BEAN, 2},           // 绿豆 → 2 号箱 (data_2)
    {BEAN_WHITE_KIDNEY_BEAN, 3}    // 白芸豆 → 3 号箱 (data_3)
};

/**
 * @brief 视觉系统主控制器
 *
 * 封装完整的比赛策略，包含：
 *   - 状态机调度（processFrame 每帧调用一次）
 *   - 取豆区 / 放置区映射表维护（扫描建图阶段）
 *   - 查表决定目标位置（任务执行阶段）
 *   - 串口收发（与电控的通信协议）
 *   - 调试信息绘制（画面标注 + 终端状态面板）
 */
class VisionController {
public:
    VisionController(const std::string& model_path, const std::string& serial_port);
    ~VisionController();

    /**
     * @brief 主入口：每帧调用一次
     *
     * 根据当前状态自动调度：
     *   - 等待状态：只检查串口反馈，不做推理
     *   - 扫描状态：执行推理 + 更新映射表
     *   - 任务状态：执行推理 + 查表 + 发送位置信号
     *
     * @param frame 相机输入帧（BGR）
     */
    void processFrame(const cv::Mat& frame);
    RobotState getState() const { return current_state; }
    void reset();

    static std::string getBeanName(int bean_class);
    static int getBoxNumberForBean(int bean_class);

private:
    RobotVision vision;                                 // YOLO 推理引擎
    std::unique_ptr<SerialPort> serial;                 // 串口通信
    std::atomic<RobotState> current_state{STATE_WAIT_START};

    // ———— 任务状态 ————
    int current_bean_class = -1;        // 当前正在处理的豆子类别（-1 = 无）
    std::string current_bean_name = ""; // 当前豆子名称

    // ———— 计数 ————
    int placed_bean_count[3] = {0, 0, 0};       // 每种豆子已放置的数量
    int target_bean_count[3] = {                // 每种豆子的目标数量（由宏定义决定）
        BEAN_SOYBEAN_TARGET_COUNT,
        BEAN_MUNG_BEAN_TARGET_COUNT,
        BEAN_WHITE_KIDNEY_TARGET_COUNT
    };

    // ———— 抓取顺序 ————
    int bean_order[3] = {               // 抓取顺序：白芸豆 → 黄豆 → 绿豆
        BEAN_WHITE_KIDNEY_BEAN,
        BEAN_SOYBEAN,
        BEAN_MUNG_BEAN
    };
    int current_order_index = 0;        // 当前在处理 bean_order 中的第几个索引

    // ============================================
    // 取豆区映射表：位置 → 豆子类别
    //
    // bean_position_to_class[3]  索引 0~2=从左到右物理位置, 值=豆子类别(0~2)
    // bean_class_to_position     反向映射: 豆子类别 → 位置编号
    // scanned_bean_positions     已经确定的位置集合 {0, 1, 2}
    // ============================================
    int bean_position_to_class[3] = {-1, -1, -1};
    std::map<int, int> bean_class_to_position;
    std::set<int> scanned_bean_positions;

    // ⭐ 位置确认重试计数（确认阶段连续失败超过 3 次则放弃本目标）
    int confirm_retry_count = 0;

    // ============================================
    // 放置区映射表：位置 → 箱号
    //
    // box_position_to_number[5]  索引 0~4=从左到右物理位置, 值=箱号(1~5)
    // box_number_to_position     反向映射: 箱号 → 位置编号
    // scanned_box_positions      已经确定的位置集合 {0, 1, 2, 3, 4}
    // ============================================
    int box_position_to_number[5] = {-1, -1, -1, -1, -1};
    std::map<int, int> box_number_to_position;
    std::set<int> scanned_box_positions;

    // ============================================
    // 当前视野信息（每帧推理后更新）
    // ============================================

    /** @brief 当前帧中检测到的豆子箱子（按画面 X 坐标排序） */
    struct BeanBoxDetection {
        int bean_class;     // 豆子类别 (0~2)
        cv::Rect bbox;      // 边界框
        cv::Point2f center; // 中心点
        float confidence;   // 置信度
    };
    std::vector<BeanBoxDetection> current_bean_boxes;

    /** @brief 当前帧中检测到的数字箱子（按画面 X 坐标排序） */
    struct NumberBoxDetection {
        int box_number;     // 箱号 (1~5)
        cv::Rect bbox;
        cv::Point2f center;
        float confidence;
    };
    std::vector<NumberBoxDetection> current_number_boxes;

    // ============================================
    // 核心处理函数
    // ============================================

    void handleScanBeanArea(const RobotVision::ClassificationResult& result);
    void handleScanBoxArea(const RobotVision::ClassificationResult& result);
    void handleIdentifyBean(const RobotVision::ClassificationResult& result);
    void handleSearchBox(const RobotVision::ClassificationResult& result);
    void handleConfirmPosition(const RobotVision::ClassificationResult& result);   // ⭐ 确认取豆目标位置
    void handleConfirmBoxPosition(const RobotVision::ClassificationResult& result); // ⭐ 确认放置目标位置
    void checkMCUFeedback();            // ⭐ 检查电控反馈（驱动状态机跳转）
    void stateTransition();

    void extractBeanBoxes(const RobotVision::ClassificationResult& result);    // 提取豆子检测框
    void extractNumberBoxes(const RobotVision::ClassificationResult& result);  // 提取数字检测框

    void updateBeanMapping();   // 扫描阶段：增量更新取豆区映射
    void updateBoxMapping();    // 扫描阶段：增量更新放置区映射

    bool isAllBeanPositionsMapped();    // 取豆区 3 个位置是否全部确定
    bool isAllBoxPositionsMapped();     // 放置区 5 个位置是否全部确定

    int getBeanPosition(int bean_class);    // 查表：豆子 → 取豆区位置编号
    int getBoxPosition(int box_number);     // 查表：箱号 → 放置区位置编号

    bool isBeanTargetInView(int target_position);   // 当前帧中是否有目标位置的豆子
    bool isBoxTargetInView(int target_position);    // 当前帧中是否有目标位置的箱子

    void printBeanMappingTable();   // 打印取豆区映射表到终端
    void printBoxMappingTable();    // 打印放置区映射表到终端

    void sendVisionResult(TargetType type, int id, bool tracking);  // 发送数据包给电控
    void drawDebugInfo(const cv::Mat& frame, const RobotVision::ClassificationResult& result);

    // ⭐ 终端显示辅助
    static void printBanner(const std::string& title, char fill = '=');
    bool statusPrintReady();            // 节流：至少间隔 1 秒才打印
    void printStatusPanel();            // 状态面板（带节流）

    // ⭐ 节流计时器
    std::chrono::steady_clock::time_point last_status_print;

    // ⭐ 启动握手：向下位机发送启动信号后，等待回复超时则重发
    std::chrono::steady_clock::time_point last_startup_signal_time;
    bool startup_signal_sent = false;

    // ———— 内联辅助函数 ————

    /**
     * @brief 获取当前需要抓取的豆子类别
     * @return 豆子类别 (0~2)，-1 表示所有豆子已完成
     *
     * 遍历 bean_order，返回第一个未完成目标数量的豆子。
     */
    inline int getCurrentTargetBeanClass() {
        for (int i = current_order_index; i < 3; i++) {
            int bean = bean_order[i];
            if (placed_bean_count[bean] < target_bean_count[bean]) {
                return bean;
            }
        }
        return -1;
    }

    /** @brief 检查指定豆子是否已完成目标数量 */
    inline bool isBeanTargetComplete(int bean_class) {
        return placed_bean_count[bean_class] >= target_bean_count[bean_class];
    }

    /** @brief 检查所有豆子是否全部完成 */
    inline bool isAllBeansComplete() {
        for (int i = 0; i < 3; i++) {
            if (placed_bean_count[i] < target_bean_count[i]) return false;
        }
        return true;
    }

    /**
     * @brief 推进到下一种未完成的豆子
     *
     * 当前豆子达到目标数量后调用，更新 current_order_index。
     * 如果所有都已完成，current_order_index 被设为 3。
     */
    inline void advanceToNextBean() {
        for (int i = current_order_index; i < 3; i++) {
            int bean = bean_order[i];
            if (placed_bean_count[bean] < target_bean_count[bean]) {
                current_order_index = i;
                return;
            }
        }
        current_order_index = 3;
    }
};

#endif