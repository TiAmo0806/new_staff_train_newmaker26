#include "../include/VisionController.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

VisionController::VisionController(const std::string& model_path, const std::string& serial_port)
    : vision(model_path) {
    serial = std::make_unique<SerialPort>(serial_port, 115200);
    if (!serial->isOpen()) {
        std::cerr << "[Controller] Failed to open serial port!" << std::endl;
    }
    
    // 重置所有计数
    for (int i = 0; i < 3; i++) {
        placed_bean_count[i] = 0;
    }
    current_order_index = 0;
    
    std::cout << "[Controller] Initialized" << std::endl;
    std::cout << "[Config] White kidney bean count: " << BEAN_WHITE_KIDNEY_TARGET_COUNT << std::endl;
    std::cout << "[Config] Soybean count: " << BEAN_SOYBEAN_TARGET_COUNT << std::endl;
    std::cout << "[Config] Mung bean count: " << BEAN_MUNG_BEAN_TARGET_COUNT << std::endl;
    std::cout << "[Config] Order: white_kidney_bean → soybean → mung_bean" << std::endl;
}

VisionController::~VisionController() {
    if (serial) serial->close();
}

void VisionController::processFrame(const cv::Mat& frame) {
    if (frame.empty()) return;
    
    if (current_state == STATE_WAIT_ACTION || current_state == STATE_IDLE) {
        return;
    }
    
    auto result = vision.infer(frame);
    
    switch (current_state.load()) {
        case STATE_IDENTIFY_BEAN:
            handleIdentifyBean(result);
            break;
        case STATE_SEARCH_EMPTY_BOX:
            handleSearchEmptyBox(result);
            break;
        case STATE_VERIFY_PLACE:
            handleVerifyPlace(result);
            break;
        default:
            break;
    }
    
    drawDebugInfo(frame, result);
}

// ============================================
// 状态1：识别豆子
// ============================================

void VisionController::handleIdentifyBean(const RobotVision::ClassificationResult& result) {
    if (isAllBeansComplete()) {
        sendVisionResult(TARGET_NONE, 0, 0, 0, false);
        current_state = STATE_IDLE;
        std::cout << "[Mission] All beans placed!" << std::endl;
        return;
    }
    
    int target_bean_class = getCurrentTargetBeanClass();
    if (target_bean_class < 0) {
        sendVisionResult(TARGET_NONE, 0, 0, 0, false);
        current_state = STATE_IDLE;
        return;
    }
    
    // 在识别结果中找目标豆子
    std::vector<RobotVision::Detection> available;
    for (const auto& bean : result.beans) {
        if (bean.class_id == target_bean_class) {
            available.push_back(bean);
        }
    }
    
    if (available.empty()) {
        sendVisionResult(TARGET_NONE, 0, 0, 0, false);
        std::cout << "[Identify] Still searching for " 
                  << getBeanName(target_bean_class) << std::endl;
        return;
    }
    
    auto best = std::max_element(available.begin(), available.end(),
        [](const auto& a, const auto& b) { return a.confidence < b.confidence; });
    
    current_bean_class = best->class_id;
    current_bean_name = best->class_name;
    
    // TODO: 像素坐标 → 角度（需要标定）
    float pitch_angle = 0.0f;
    float yaw_angle = 0.0f;
    
    sendVisionResult(TARGET_BEAN, current_bean_class, pitch_angle, yaw_angle, true);
    
    std::cout << "[Identify] Found " << current_bean_name 
              << " (抓取进度: " << placed_bean_count[current_bean_class] 
              << "/" << target_bean_count[current_bean_class] << ")" << std::endl;
    current_state = STATE_WAIT_ACTION;
}

// ============================================
// 状态2：找空箱子
// ============================================

void VisionController::handleSearchEmptyBox(const RobotVision::ClassificationResult& result) {
    int target_box = getBoxNumberForBean(current_bean_class);
    
    for (const auto& det : result.target_digits) {
        int digit = vision.getDigitValue(det.class_id);
        if (digit == target_box) {
            float pitch_angle = 0.0f;
            float yaw_angle = 0.0f;
            
            sendVisionResult(TARGET_EMPTY_BOX, target_box, pitch_angle, yaw_angle, true);
            
            std::cout << "[SearchEmpty] Found box " << target_box 
                      << " for " << current_bean_name << std::endl;
            current_state = STATE_WAIT_ACTION;
            return;
        }
    }
    
    sendVisionResult(TARGET_NONE, 0, 0, 0, false);
    std::cout << "[SearchEmpty] Still searching for box " << target_box << std::endl;
}

// ============================================
// 状态3：验证放置
// ============================================

void VisionController::handleVerifyPlace(const RobotVision::ClassificationResult& result) {
    bool bean_in_gripper = false;
    for (const auto& bean : result.beans) {
        if (gripper_roi.contains(cv::Point(bean.center.x, bean.center.y))) {
            bean_in_gripper = true;
            break;
        }
    }
    
    if (bean_in_gripper) {
        std::cout << "[Verify] Failed to place " << current_bean_name << ", retrying..." << std::endl;
        int target_box = getBoxNumberForBean(current_bean_class);
        sendVisionResult(TARGET_EMPTY_BOX, target_box, 0, 0, true);
    } else {
        // 放置成功：计数+1
        placed_bean_count[current_bean_class]++;
        
        std::cout << "[Verify] Successfully placed " << current_bean_name 
                  << " into box " << getBoxNumberForBean(current_bean_class)
                  << " (完成: " << placed_bean_count[current_bean_class] 
                  << "/" << target_bean_count[current_bean_class] << ")" << std::endl;
        
        if (isBeanTargetComplete(current_bean_class)) {
            advanceToNextBean();
            std::cout << "[Progress] " << current_bean_name << " 已完成! 移到下一个目标" << std::endl;
        }
        
        current_bean_class = -1;
        current_state = STATE_IDENTIFY_BEAN;
        std::cout << "[State] Back to IDENTIFY_BEAN" << std::endl;
    }
}

// ============================================
// 状态转换
// ============================================

void VisionController::stateTransition() {
    if (current_state == STATE_WAIT_ACTION) {
        current_state = STATE_VERIFY_PLACE;
        std::cout << "[State] WAIT → VERIFY_PLACE" << std::endl;
    }
}

// ============================================
// 发送数据包
// ============================================

void VisionController::sendVisionResult(TargetType type, int id, float pitch, float yaw, bool tracking) {
    if (!serial || !serial->isOpen()) return;
    
    rm_serial_driver::VisionToMCUPacket packet;
    packet.header = 0xA6;
    packet.target_type = static_cast<uint8_t>(type);
    packet.target_id = static_cast<uint8_t>(id);
    packet.tracking = tracking ? 1 : 0;
    packet.reserved = 0;
    packet.pitch_cmd = pitch;
    packet.yaw_cmd = yaw;
    packet.checksum = 0;
    
    serial->sendPacket(packet);
}

// ============================================
// 重置
// ============================================

void VisionController::reset() {
    current_state = STATE_IDENTIFY_BEAN;
    current_bean_class = -1;
    current_bean_name = "";
    current_order_index = 0;
    
    for (int i = 0; i < 3; i++) {
        placed_bean_count[i] = 0;
    }
    
    std::cout << "[Controller] Reset" << std::endl;
}

// ============================================
// 调试显示
// ============================================

void VisionController::drawDebugInfo(const cv::Mat& frame, const RobotVision::ClassificationResult& result) {
    cv::Mat display = frame.clone();
    
    // 绘制豆子
    for (const auto& det : result.beans) {
        cv::Scalar color;
        bool is_done = isBeanTargetComplete(det.class_id);
        
        if (is_done) {
            color = cv::Scalar(128, 128, 128);  // 灰色 = 已完成
        } else if (det.class_id == getCurrentTargetBeanClass()) {
            color = cv::Scalar(0, 255, 0);      // 亮绿 = 当前目标
        } else {
            color = cv::Scalar(0, 200, 100);    // 暗绿 = 其他
        }
        
        cv::rectangle(display, det.bbox, color, 2);
        std::string label = getBeanName(det.class_id);
        if (is_done) label += " ✓";
        cv::putText(display, label, cv::Point(det.bbox.x, det.bbox.y-10),
                   cv::FONT_HERSHEY_SIMPLEX, 0.5, color, 2);
    }
    
    // 绘制目标数字
    for (const auto& det : result.target_digits) {
        cv::rectangle(display, det.bbox, cv::Scalar(255, 0, 0), 2);
        std::string label = "data_" + std::to_string(vision.getDigitValue(det.class_id));
        cv::putText(display, label, cv::Point(det.bbox.x, det.bbox.y-10),
                   cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 0, 0), 2);
    }
    
    // 绘制忽略数字
    for (const auto& det : result.ignore_digits) {
        cv::rectangle(display, det.bbox, cv::Scalar(0, 0, 255), 2);
        cv::putText(display, "IGNORE", cv::Point(det.bbox.x, det.bbox.y-10),
                   cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 255), 2);
    }
    
    // 状态
    const char* states[] = {"IDENTIFY_BEAN", "SEARCH_EMPTY", "WAIT", "VERIFY", "IDLE"};
    cv::putText(display, "State: " + std::string(states[current_state.load()]),
               cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 255), 2);
    
    // 当前目标
    int current_target = getCurrentTargetBeanClass();
    std::string target_info = "Current: ";
    if (current_target >= 0) {
        target_info += getBeanName(current_target);
        target_info += " (" + std::to_string(placed_bean_count[current_target]) + 
                       "/" + std::to_string(target_bean_count[current_target]) + ")";
    } else {
        target_info += "None (All done!)";
    }
    cv::putText(display, target_info, cv::Point(10, 60),
               cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 255, 0), 2);
    
    // 进度
    std::string progress = "Progress: ";
    for (int i = 0; i < 3; i++) {
        progress += getBeanName(i) + ":" + std::to_string(placed_bean_count[i]) + 
                    "/" + std::to_string(target_bean_count[i]) + " ";
    }
    cv::putText(display, progress, cv::Point(10, 90),
               cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(255, 255, 255), 1);
    
    cv::rectangle(display, gripper_roi, cv::Scalar(255, 255, 255), 1);
    cv::imshow("Robot Vision", display);
    cv::waitKey(1);
}