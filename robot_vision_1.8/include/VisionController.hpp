#ifndef VISION_CONTROLLER_HPP
#define VISION_CONTROLLER_HPP

#include "RobotVision.hpp"
#include "SerialPort.hpp"
#include <memory>
#include <vector>
#include <string>
#include <atomic>

// ============================================
// ⭐ 抓取参数配置（比赛时在这里修改）
// ============================================
#define BEAN_WHITE_KIDNEY_TARGET_COUNT  1   // 白芸豆抓取次数（先抓）[索引2]
#define BEAN_SOYBEAN_TARGET_COUNT       1   // 黄豆抓取次数（其次）  [索引0]
#define BEAN_MUNG_BEAN_TARGET_COUNT     1   // 绿豆抓取次数（最后）  [索引1]
// ============================================

enum TargetType : uint8_t {
    TARGET_NONE = 0,
    TARGET_BEAN = 1,
    TARGET_EMPTY_BOX = 2,
};

enum BeanClass : int {
    BEAN_SOYBEAN = 0,           // 黄豆
    BEAN_MUNG_BEAN = 1,         // 绿豆
    BEAN_WHITE_KIDNEY_BEAN = 2  // 白芸豆
};

enum RobotState {
    STATE_IDENTIFY_BEAN,
    STATE_SEARCH_EMPTY_BOX,
    STATE_WAIT_ACTION,
    STATE_VERIFY_PLACE,
    STATE_IDLE
};

class VisionController {
public:
    VisionController(const std::string& model_path, const std::string& serial_port);
    ~VisionController();

    void processFrame(const cv::Mat& frame);
    RobotState getState() const { return current_state; }
    void reset();

    // 豆子类别 → 箱子编号（0→1号箱，1→2号箱，2→3号箱）
    static int getBoxNumberForBean(int bean_class) {
        return bean_class + 1;
    }

    // 获取豆子名称
    static std::string getBeanName(int bean_class) {
        switch (bean_class) {
            case BEAN_SOYBEAN: return "soybean";
            case BEAN_MUNG_BEAN: return "mung_bean";
            case BEAN_WHITE_KIDNEY_BEAN: return "white_kidney_bean";
            default: return "unknown";
        }
    }

private:
    RobotVision vision;
    std::unique_ptr<SerialPort> serial;
    std::atomic<RobotState> current_state{STATE_IDENTIFY_BEAN};
    
    // 任务状态
    int current_bean_class = -1;
    std::string current_bean_name = "";
    
    // ⭐ 已放置计数：数组索引 = 豆子类别 (0=黄豆, 1=绿豆, 2=白芸豆)
    int placed_bean_count[3] = {0, 0, 0};
    
    // ⭐ 目标抓取次数：数组索引 = 豆子类别
    // 索引0=黄豆, 索引1=绿豆, 索引2=白芸豆
    int target_bean_count[3] = {
        BEAN_SOYBEAN_TARGET_COUNT,        // 黄豆抓取次数
        BEAN_MUNG_BEAN_TARGET_COUNT,      // 绿豆抓取次数
        BEAN_WHITE_KIDNEY_TARGET_COUNT    // 白芸豆抓取次数
    };
    
    // ⭐ 抓取顺序（先白芸豆[2]，再黄豆[0]，最后绿豆[1]）
    int bean_order[3] = {
        BEAN_WHITE_KIDNEY_BEAN,  // 索引0：先抓白芸豆
        BEAN_SOYBEAN,            // 索引1：再抓黄豆
        BEAN_MUNG_BEAN           // 索引2：最后抓绿豆
    };
    int current_order_index = 0;
    
    cv::Rect gripper_roi = cv::Rect(280, 200, 80, 80);
    
    void handleIdentifyBean(const RobotVision::ClassificationResult& result);
    void handleSearchEmptyBox(const RobotVision::ClassificationResult& result);
    void handleVerifyPlace(const RobotVision::ClassificationResult& result);
    void stateTransition();
    void sendVisionResult(TargetType type, int id, float pitch, float yaw, bool tracking);
    void drawDebugInfo(const cv::Mat& frame, const RobotVision::ClassificationResult& result);
    
    // 辅助函数（内联定义，直接在头文件里实现）
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
            if (placed_bean_count[i] < target_bean_count[i]) {
                return false;
            }
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