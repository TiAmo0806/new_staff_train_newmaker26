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

// ============================================
// ⭐ 抓取参数配置（比赛时在这里修改）
// ============================================
#define BEAN_WHITE_KIDNEY_TARGET_COUNT  1   // 白芸豆抓取次数（先抓）
#define BEAN_SOYBEAN_TARGET_COUNT       1   // 黄豆抓取次数（其次）
#define BEAN_MUNG_BEAN_TARGET_COUNT     1   // 绿豆抓取次数（最后）
// ============================================

enum TargetType : uint8_t {
    TARGET_NONE = 0,
    TARGET_BEAN_BOX = 1,        // 装豆子的箱子（取豆区）
    TARGET_BOX = 2,             // 贴数字的箱子（放置区）
};

enum BeanClass : int {
    BEAN_SOYBEAN = 0,
    BEAN_MUNG_BEAN = 1,
    BEAN_WHITE_KIDNEY_BEAN = 2
};

// 状态枚举
enum RobotState {
    STATE_SCAN_BEAN_AREA,       // 扫描取豆区
    STATE_SCAN_BOX_AREA,        // 扫描放置区
    STATE_IDENTIFY_BEAN,        // 识别取豆区箱子位置
    STATE_SEARCH_BOX,           // 找放置区箱子位置
    STATE_WAIT_ACTION,          // 等待电控执行
    STATE_IDLE                  // 任务完成
};

// 豆子 → 目标箱号映射
static const std::map<int, int> BEAN_TO_BOX = {
    {BEAN_SOYBEAN, 1},
    {BEAN_MUNG_BEAN, 2},
    {BEAN_WHITE_KIDNEY_BEAN, 3}
};

class VisionController {
public:
    VisionController(const std::string& model_path, const std::string& serial_port);
    ~VisionController();

    void processFrame(const cv::Mat& frame);
    RobotState getState() const { return current_state; }
    void reset();

    static std::string getBeanName(int bean_class);
    static int getBoxNumberForBean(int bean_class);

private:
    RobotVision vision;
    std::unique_ptr<SerialPort> serial;
    std::atomic<RobotState> current_state{STATE_SCAN_BEAN_AREA};
    
    // 任务状态
    int current_bean_class = -1;
    std::string current_bean_name = "";
    
    // 计数
    int placed_bean_count[3] = {0, 0, 0};
    int target_bean_count[3] = {
        BEAN_SOYBEAN_TARGET_COUNT,
        BEAN_MUNG_BEAN_TARGET_COUNT,
        BEAN_WHITE_KIDNEY_TARGET_COUNT
    };
    
    // 抓取顺序
    int bean_order[3] = {
        BEAN_WHITE_KIDNEY_BEAN,
        BEAN_SOYBEAN,
        BEAN_MUNG_BEAN
    };
    int current_order_index = 0;
    
    // ============================================
    // 取豆区映射表：位置 → 豆子类别
    // ============================================
    int bean_position_to_class[3] = {-1, -1, -1};
    std::map<int, int> bean_class_to_position;
    std::set<int> scanned_bean_positions;
    
    // ============================================
    // 放置区映射表：位置 → 箱号
    // ============================================
    int box_position_to_number[5] = {-1, -1, -1, -1, -1};
    std::map<int, int> box_number_to_position;
    std::set<int> scanned_box_positions;
    
    // ============================================
    // 当前视野信息
    // ============================================

    struct BeanBoxDetection {
        int bean_class;
        cv::Rect bbox;
        cv::Point2f center;
        float confidence;
    };
    std::vector<BeanBoxDetection> current_bean_boxes;

    struct NumberBoxDetection {
        int box_number;
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
    void checkMCUFeedback();  // ⭐ 新增：检查电控反馈
    void stateTransition();
    
    void extractBeanBoxes(const RobotVision::ClassificationResult& result);
    void extractNumberBoxes(const RobotVision::ClassificationResult& result);
    
    void updateBeanMapping();
    void updateBoxMapping();
    
    bool isAllBeanPositionsMapped();
    bool isAllBoxPositionsMapped();
    
    int getBeanPosition(int bean_class);
    int getBoxPosition(int box_number);
    
    bool isBeanTargetInView(int target_position);
    bool isBoxTargetInView(int target_position);
    
    void printBeanMappingTable();
    void printBoxMappingTable();
    
    void sendVisionResult(TargetType type, int id, bool tracking);
    void drawDebugInfo(const cv::Mat& frame, const RobotVision::ClassificationResult& result);
    
    // 辅助函数
    inline int getCurrentTargetBeanClass() {
        for (int i = current_order_index; i < 3; i++) {
            int bean = bean_order[i];
            if (placed_bean_count[bean] < target_bean_count[bean]) {
                return bean;
            }
        }
        return -1;
    }
    
    inline bool isBeanTargetComplete(int bean_class) {
        return placed_bean_count[bean_class] >= target_bean_count[bean_class];
    }
    
    inline bool isAllBeansComplete() {
        for (int i = 0; i < 3; i++) {
            if (placed_bean_count[i] < target_bean_count[i]) return false;
        }
        return true;
    }
    
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