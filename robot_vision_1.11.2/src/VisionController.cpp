#include "../include/VisionController.hpp"
#include "../include/Visualization.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <iomanip>

// ============================================
// 构造函数
// ============================================

VisionController::VisionController(const std::string& model_path, const std::string& serial_port)
    : vision(model_path) {
    serial = std::make_unique<SerialPort>(serial_port, 115200);
    if (!serial->isOpen()) {
        std::cerr << "[Controller] Failed to open serial port!" << std::endl;
    }

    // 重置计数
    for (int i = 0; i < 3; i++) {
        placed_bean_count[i] = 0;
    }
    current_order_index = 0;

    // 重置取豆区映射
    for (int i = 0; i < 3; i++) {
        bean_position_to_class[i] = -1;
    }
    bean_class_to_position.clear();
    scanned_bean_positions.clear();

    // 重置放置区映射
    for (int i = 0; i < 5; i++) {
        box_position_to_number[i] = -1;
    }
    box_number_to_position.clear();
    scanned_box_positions.clear();

    std::cout << "[Controller] Initialized" << std::endl;
    std::cout << "[Config] White kidney bean count: " << BEAN_WHITE_KIDNEY_TARGET_COUNT << std::endl;
    std::cout << "[Config] Soybean count: " << BEAN_SOYBEAN_TARGET_COUNT << std::endl;
    std::cout << "[Config] Mung bean count: " << BEAN_MUNG_BEAN_TARGET_COUNT << std::endl;
    std::cout << "[Config] Order: white_kidney_bean → soybean → mung_bean" << std::endl;
    std::cout << "[State] Starting SCAN_BEAN_AREA... Please rotate camera slowly from left to right" << std::endl;
}

VisionController::~VisionController() {
    if (serial) serial->close();
}

// ============================================
// 静态辅助函数
// ============================================

std::string VisionController::getBeanName(int bean_class) {
    switch (bean_class) {
        case BEAN_SOYBEAN: return "soybean";
        case BEAN_MUNG_BEAN: return "mung_bean";
        case BEAN_WHITE_KIDNEY_BEAN: return "white_kidney_bean";
        default: return "unknown";
    }
}

int VisionController::getBoxNumberForBean(int bean_class) {
    auto it = BEAN_TO_BOX.find(bean_class);
    if (it != BEAN_TO_BOX.end()) {
        return it->second;
    }
    return -1;
}

// ============================================
// ⭐ 主循环
// ============================================

void VisionController::processFrame(const cv::Mat& frame) {
    if (frame.empty()) return;

    // ⭐ 如果在等待状态，检查电控反馈
    if (current_state == STATE_WAIT_ACTION) {
        checkMCUFeedback();
        return;
    }

    if (current_state == STATE_IDLE) {
        return;
    }

    auto result = vision.infer(frame);

    extractBeanBoxes(result);
    extractNumberBoxes(result);

    switch (current_state.load()) {
        case STATE_SCAN_BEAN_AREA:
            handleScanBeanArea(result);
            break;
        case STATE_SCAN_BOX_AREA:
            handleScanBoxArea(result);
            break;
        case STATE_IDENTIFY_BEAN:
            handleIdentifyBean(result);
            break;
        case STATE_SEARCH_BOX:
            handleSearchBox(result);
            break;
        default:
            break;
    }

    drawDebugInfo(frame, result);
}

// ============================================
// ⭐ 检查电控反馈
// ============================================

void VisionController::checkMCUFeedback() {
    if (!serial || !serial->isOpen()) return;

    rm_serial_driver::MCUToVisionPacket feedback;
    if (serial->receivePacket(feedback, 50)) {
        if (feedback.feedback_type == 1) {
            // 抓取完成 → 进入找箱子状态
            current_state = STATE_SEARCH_BOX;
            std::cout << "[State] WAIT(抓取完成) → SEARCH_BOX" << std::endl;
        } else if (feedback.feedback_type == 2) {
            // 放置完成 → 计数，回到识别状态
            placed_bean_count[current_bean_class]++;

            if (isBeanTargetComplete(current_bean_class)) {
                advanceToNextBean();
                std::cout << "[Progress] " << current_bean_name << " 完成!" << std::endl;
            }

            current_bean_class = -1;
            current_state = STATE_IDENTIFY_BEAN;
            std::cout << "[State] WAIT(放置完成) → IDENTIFY_BEAN" << std::endl;
        } else if (feedback.feedback_type == 3) {
            // 移动完成 → 根据之前的状态继续
            std::cout << "[State] MCU移动完成" << std::endl;
        } else if (feedback.feedback_type == 4) {
            // 动作失败 → 重发当前目标
            std::cout << "[State] Action failed, retrying..." << std::endl;
            if (current_bean_class >= 0) {
                int pos = getBeanPosition(current_bean_class);
                if (pos >= 0) {
                    sendVisionResult(TARGET_BEAN_BOX, pos, true);
                }
            }
        }
    }
}

// ============================================
// 提取取豆区豆子箱子信息
// ============================================

void VisionController::extractBeanBoxes(const RobotVision::ClassificationResult& result) {
    current_bean_boxes.clear();

    for (const auto& det : result.beans) {
        // ⭐ 过滤太小的框（噪声误识别），要求至少 20x20 像素
        if (det.bbox.width < 20 || det.bbox.height < 20) {
            std::cout << "[Extract] Ignoring tiny bean box: "
                      << det.class_name << " " << det.bbox.width << "x" << det.bbox.height
                      << " conf=" << det.confidence << std::endl;
            continue;
        }
        BeanBoxDetection box;
        box.bean_class = det.class_id;
        box.bbox = det.bbox;
        box.center = det.center;
        box.confidence = det.confidence;
        current_bean_boxes.push_back(box);
    }

    std::sort(current_bean_boxes.begin(), current_bean_boxes.end(),
        [](const BeanBoxDetection& a, const BeanBoxDetection& b) {
            return a.center.x < b.center.x;
        });
}

// ============================================
// 提取放置区数字箱子信息
// ============================================

void VisionController::extractNumberBoxes(const RobotVision::ClassificationResult& result) {
    current_number_boxes.clear();

    for (const auto& det : result.digits) {
        // 过滤太小的框（噪声）
        if (det.bbox.width < 6 || det.bbox.height < 6) continue;

        NumberBoxDetection box;
        box.box_number = vision.getDigitValue(det.class_id);
        box.bbox = det.bbox;
        box.center = det.center;
        box.confidence = det.confidence;
        current_number_boxes.push_back(box);
    }

    std::sort(current_number_boxes.begin(), current_number_boxes.end(),
        [](const NumberBoxDetection& a, const NumberBoxDetection& b) {
            return a.center.x < b.center.x;
        });
}

// ============================================
// 阶段1：扫描取豆区
// ============================================

void VisionController::handleScanBeanArea(const RobotVision::ClassificationResult& result) {
    if (isAllBeanPositionsMapped()) {
        current_state = STATE_SCAN_BOX_AREA;
        std::cout << "[State] SCAN_BEAN_AREA complete! → SCAN_BOX_AREA" << std::endl;
        printBeanMappingTable();
        std::cout << "[State] Now scanning box area... Please rotate camera slowly from left to right" << std::endl;
        return;
    }

    updateBeanMapping();

    if (isAllBeanPositionsMapped()) {
        current_state = STATE_SCAN_BOX_AREA;
        std::cout << "[State] SCAN_BEAN_AREA complete! → SCAN_BOX_AREA" << std::endl;
        printBeanMappingTable();
        std::cout << "[State] Now scanning box area... Please rotate camera slowly from left to right" << std::endl;
        return;
    }

    sendVisionResult(TARGET_NONE, -1, false);
    std::cout << "[ScanBean] Mapped " << scanned_bean_positions.size() << "/3 positions. Keep rotating..." << std::endl;
}

// ============================================
// 更新取豆区映射
// ============================================

void VisionController::updateBeanMapping() {
    if (current_bean_boxes.empty()) return;

    for (size_t i = 0; i < current_bean_boxes.size(); i++) {
        int bean_class = current_bean_boxes[i].bean_class;

        if (bean_class_to_position.find(bean_class) != bean_class_to_position.end()) {
            continue;
        }

        int estimated_position = -1;
        int left_mapped_idx = -1;   // 实际已映射框的索引
        int right_mapped_idx = -1;  // 实际已映射框的索引

        int left_mapped_pos = -1;
        for (int j = (int)i - 1; j >= 0; j--) {
            int left_bean = current_bean_boxes[j].bean_class;
            if (bean_class_to_position.find(left_bean) != bean_class_to_position.end()) {
                left_mapped_pos = bean_class_to_position[left_bean];
                left_mapped_idx = j;  // ⭐ 记录实际索引
                break;
            }
        }

        int right_mapped_pos = -1;
        for (size_t j = i + 1; j < current_bean_boxes.size(); j++) {
            int right_bean = current_bean_boxes[j].bean_class;
            if (bean_class_to_position.find(right_bean) != bean_class_to_position.end()) {
                right_mapped_pos = bean_class_to_position[right_bean];
                right_mapped_idx = j;  // ⭐ 记录实际索引
                break;
            }
        }

        if (left_mapped_pos >= 0 && right_mapped_pos >= 0) {
            // ⭐ 修复: 使用实际映射框的坐标，而非假设的 i-1/i+1
            float left_x = current_bean_boxes[left_mapped_idx].center.x;
            float right_x = current_bean_boxes[right_mapped_idx].center.x;
            float current_x = current_bean_boxes[i].center.x;
            float ratio = (current_x - left_x) / (right_x - left_x);
            estimated_position = left_mapped_pos + (int)std::round(ratio * (right_mapped_pos - left_mapped_pos));
        } else if (left_mapped_pos >= 0) {
            int gap = 1;
            for (int j = (int)i - 1; j >= 0; j--) {
                int check_bean = current_bean_boxes[j].bean_class;
                if (bean_class_to_position.find(check_bean) == bean_class_to_position.end()) {
                    gap++;
                } else {
                    break;
                }
            }
            estimated_position = left_mapped_pos + gap;
        } else if (right_mapped_pos >= 0) {
            int gap = 1;
            for (size_t j = i + 1; j < current_bean_boxes.size(); j++) {
                int check_bean = current_bean_boxes[j].bean_class;
                if (bean_class_to_position.find(check_bean) == bean_class_to_position.end()) {
                    gap++;
                } else {
                    break;
                }
            }
            estimated_position = right_mapped_pos - gap;
        } else {
            estimated_position = 0;
        }

        if (estimated_position >= 0 && estimated_position < 3 &&
            bean_position_to_class[estimated_position] == -1) {
            bean_position_to_class[estimated_position] = bean_class;
            bean_class_to_position[bean_class] = estimated_position;
            scanned_bean_positions.insert(estimated_position);
            std::cout << "[BeanMapping] " << getBeanName(bean_class)
                      << " → position " << estimated_position << std::endl;
        }
    }
}

// ============================================
// 检查取豆区是否全部映射完成
// ============================================

bool VisionController::isAllBeanPositionsMapped() {
    for (int pos = 0; pos < 3; pos++) {
        if (bean_position_to_class[pos] == -1) {
            return false;
        }
    }
    return true;
}

// ============================================
// 打印取豆区映射表
// ============================================

void VisionController::printBeanMappingTable() {
    std::cout << "========================================" << std::endl;
    std::cout << "      Bean Area Mapping Table           " << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Position  |  Bean Type                  " << std::endl;
    std::cout << "----------|-----------------------------" << std::endl;
    for (int i = 0; i < 3; i++) {
        int bean = bean_position_to_class[i];
        std::string name = (bean >= 0) ? getBeanName(bean) : "EMPTY";
        std::cout << "   " << i << "     |  " << name << std::endl;
    }
    std::cout << "========================================" << std::endl;
}

// ============================================
// 获取取豆区位置
// ============================================

int VisionController::getBeanPosition(int bean_class) {
    auto it = bean_class_to_position.find(bean_class);
    if (it != bean_class_to_position.end()) {
        return it->second;
    }
    return -1;
}

// ============================================
// 判断豆子目标是否在视野中
// ============================================

bool VisionController::isBeanTargetInView(int target_position) {
    for (const auto& box : current_bean_boxes) {
        int pos = getBeanPosition(box.bean_class);
        if (pos == target_position) {
            return true;
        }
    }
    return false;
}

// ============================================
// 阶段2：扫描放置区
// ============================================

void VisionController::handleScanBoxArea(const RobotVision::ClassificationResult& result) {
    if (isAllBoxPositionsMapped()) {
        current_state = STATE_IDENTIFY_BEAN;
        std::cout << "[State] SCAN_BOX_AREA complete! → IDENTIFY_BEAN" << std::endl;
        printBoxMappingTable();
        return;
    }

    updateBoxMapping();

    if (isAllBoxPositionsMapped()) {
        current_state = STATE_IDENTIFY_BEAN;
        std::cout << "[State] SCAN_BOX_AREA complete! → IDENTIFY_BEAN" << std::endl;
        printBoxMappingTable();
        return;
    }

    sendVisionResult(TARGET_NONE, -1, false);
    std::cout << "[ScanBox] Mapped " << scanned_box_positions.size() << "/5 positions. Keep rotating..." << std::endl;
}

// ============================================
// 更新放置区映射
// ============================================

void VisionController::updateBoxMapping() {
    if (current_number_boxes.empty()) return;

    for (size_t i = 0; i < current_number_boxes.size(); i++) {
        int box_num = current_number_boxes[i].box_number;

        if (box_number_to_position.find(box_num) != box_number_to_position.end()) {
            continue;
        }

        int estimated_position = -1;
        int left_mapped_idx = -1;   // 实际已映射框的索引
        int right_mapped_idx = -1;  // 实际已映射框的索引

        int left_mapped_pos = -1;
        for (int j = (int)i - 1; j >= 0; j--) {
            int left_box = current_number_boxes[j].box_number;
            if (box_number_to_position.find(left_box) != box_number_to_position.end()) {
                left_mapped_pos = box_number_to_position[left_box];
                left_mapped_idx = j;  // ⭐ 记录实际索引
                break;
            }
        }

        int right_mapped_pos = -1;
        for (size_t j = i + 1; j < current_number_boxes.size(); j++) {
            int right_box = current_number_boxes[j].box_number;
            if (box_number_to_position.find(right_box) != box_number_to_position.end()) {
                right_mapped_pos = box_number_to_position[right_box];
                right_mapped_idx = j;  // ⭐ 记录实际索引
                break;
            }
        }

        if (left_mapped_pos >= 0 && right_mapped_pos >= 0) {
            // ⭐ 修复: 使用实际映射框的坐标，而非假设的 i-1/i+1
            float left_x = current_number_boxes[left_mapped_idx].center.x;
            float right_x = current_number_boxes[right_mapped_idx].center.x;
            float current_x = current_number_boxes[i].center.x;
            float ratio = (current_x - left_x) / (right_x - left_x);
            estimated_position = left_mapped_pos + (int)std::round(ratio * (right_mapped_pos - left_mapped_pos));
        } else if (left_mapped_pos >= 0) {
            int gap = 1;
            for (int j = (int)i - 1; j >= 0; j--) {
                int check_box = current_number_boxes[j].box_number;
                if (box_number_to_position.find(check_box) == box_number_to_position.end()) {
                    gap++;
                } else {
                    break;
                }
            }
            estimated_position = left_mapped_pos + gap;
        } else if (right_mapped_pos >= 0) {
            int gap = 1;
            for (size_t j = i + 1; j < current_number_boxes.size(); j++) {
                int check_box = current_number_boxes[j].box_number;
                if (box_number_to_position.find(check_box) == box_number_to_position.end()) {
                    gap++;
                } else {
                    break;
                }
            }
            estimated_position = right_mapped_pos - gap;
        } else {
            estimated_position = 0;
        }

        if (estimated_position >= 0 && estimated_position < 5 &&
            box_position_to_number[estimated_position] == -1) {
            box_position_to_number[estimated_position] = box_num;
            box_number_to_position[box_num] = estimated_position;
            scanned_box_positions.insert(estimated_position);
            std::cout << "[BoxMapping] data_" << box_num
                      << " → position " << estimated_position << std::endl;
        }
    }
}

// ============================================
// 检查放置区是否全部映射完成
// ============================================

bool VisionController::isAllBoxPositionsMapped() {
    for (int pos = 0; pos < 5; pos++) {
        if (box_position_to_number[pos] == -1) {
            return false;
        }
    }
    return true;
}

// ============================================
// 打印放置区映射表
// ============================================

void VisionController::printBoxMappingTable() {
    std::cout << "========================================" << std::endl;
    std::cout << "      Box Area Mapping Table            " << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Position  |  Box Number                 " << std::endl;
    std::cout << "----------|-----------------------------" << std::endl;
    for (int i = 0; i < 5; i++) {
        int box = box_position_to_number[i];
        std::string name = (box >= 0) ? "data_" + std::to_string(box) : "EMPTY";
        std::cout << "   " << i << "     |  " << name << std::endl;
    }
    std::cout << "========================================" << std::endl;
}

// ============================================
// 获取放置区位置
// ============================================

int VisionController::getBoxPosition(int box_number) {
    auto it = box_number_to_position.find(box_number);
    if (it != box_number_to_position.end()) {
        return it->second;
    }
    return -1;
}

// ============================================
// 判断箱子目标是否在视野中
// ============================================

bool VisionController::isBoxTargetInView(int target_position) {
    for (const auto& box : current_number_boxes) {
        int pos = getBoxPosition(box.box_number);
        if (pos == target_position) {
            return true;
        }
    }
    return false;
}

// ============================================
// 状态3：识别取豆区箱子位置（正常任务）
// ============================================

void VisionController::handleIdentifyBean(const RobotVision::ClassificationResult& result) {
    if (isAllBeansComplete()) {
        sendVisionResult(TARGET_NONE, -1, false);
        current_state = STATE_IDLE;
        std::cout << "[Mission] All beans placed!" << std::endl;
        return;
    }

    int target_bean_class = getCurrentTargetBeanClass();
    if (target_bean_class < 0) {
        sendVisionResult(TARGET_NONE, -1, false);
        current_state = STATE_IDLE;
        return;
    }

    int target_position = getBeanPosition(target_bean_class);
    if (target_position < 0) {
        std::cout << "[Identify] ERROR: " << getBeanName(target_bean_class)
                  << " not mapped!" << std::endl;
        sendVisionResult(TARGET_NONE, -1, false);
        return;
    }

    if (!isBeanTargetInView(target_position)) {
        sendVisionResult(TARGET_NONE, -1, false);
        std::cout << "[Identify] Target position " << target_position
                  << " not in view, rotating..." << std::endl;
        return;
    }

    current_bean_class = target_bean_class;
    current_bean_name = getBeanName(target_bean_class);

    sendVisionResult(TARGET_BEAN_BOX, target_position, true);

    std::cout << "[Identify] Found " << current_bean_name
              << " at position " << target_position
              << " (抓取进度: " << placed_bean_count[current_bean_class]
              << "/" << target_bean_count[current_bean_class] << ")" << std::endl;
    current_state = STATE_WAIT_ACTION;
}

// ============================================
// 状态4：找放置区箱子位置（正常任务）
// ============================================

void VisionController::handleSearchBox(const RobotVision::ClassificationResult& result) {
    int target_box_number = getBoxNumberForBean(current_bean_class);
    if (target_box_number < 0) {
        sendVisionResult(TARGET_NONE, -1, false);
        return;
    }

    int target_position = getBoxPosition(target_box_number);
    if (target_position < 0) {
        sendVisionResult(TARGET_NONE, -1, false);
        std::cout << "[SearchBox] ERROR: box " << target_box_number << " not mapped!" << std::endl;
        return;
    }

    if (!isBoxTargetInView(target_position)) {
        sendVisionResult(TARGET_NONE, -1, false);
        std::cout << "[SearchBox] Target position " << target_position
                  << " not in view, rotating..." << std::endl;
        return;
    }

    sendVisionResult(TARGET_BOX, target_position, true);

    std::cout << "[SearchBox] Found box " << target_box_number
              << " at position " << target_position << std::endl;

    current_state = STATE_WAIT_ACTION;
}

// ============================================
// 状态转换（保留备用，实际已用 feedback 替代）
// ============================================

void VisionController::stateTransition() {
    // 已由 checkMCUFeedback() 处理
}

// ============================================
// 发送数据包
// ============================================

void VisionController::sendVisionResult(TargetType type, int id, bool tracking) {
    if (!serial || !serial->isOpen()) return;

    rm_serial_driver::VisionToMCUPacket packet;
    packet.header = 0xA6;
    packet.target_type = static_cast<uint8_t>(type);
    packet.target_id = static_cast<uint8_t>(id < 0 ? 0 : id);
    packet.tracking = tracking ? 1 : 0;
    packet.reserved = 0;
    packet.pitch_cmd = 0.0f;
    packet.yaw_cmd = 0.0f;
    packet.checksum = 0;

    serial->sendPacket(packet);
}

// ============================================
// 重置
// ============================================

void VisionController::reset() {
    current_state = STATE_SCAN_BEAN_AREA;
    current_bean_class = -1;
    current_bean_name = "";
    current_order_index = 0;
    current_bean_boxes.clear();
    current_number_boxes.clear();

    for (int i = 0; i < 3; i++) {
        placed_bean_count[i] = 0;
        bean_position_to_class[i] = -1;
    }
    bean_class_to_position.clear();
    scanned_bean_positions.clear();

    for (int i = 0; i < 5; i++) {
        box_position_to_number[i] = -1;
    }
    box_number_to_position.clear();
    scanned_box_positions.clear();

    std::cout << "[Controller] Reset" << std::endl;
    std::cout << "[State] Starting SCAN_BEAN_AREA... Please rotate camera slowly from left to right" << std::endl;
}

// ============================================
// 调试显示
// ============================================

void VisionController::drawDebugInfo(const cv::Mat& frame, const RobotVision::ClassificationResult& result) {
    cv::Mat display = frame.clone();

    // ---- 构建 BeanBoxDisplayInfo 列表 ----
    std::vector<BeanBoxDisplayInfo> beanInfo;
    for (const auto& box : current_bean_boxes) {
        BeanBoxDisplayInfo info;
        info.class_id   = box.bean_class;
        info.position   = getBeanPosition(box.bean_class);
        info.confidence = box.confidence;
        info.bbox       = box.bbox;
        info.label      = getBeanName(box.bean_class);
        info.is_target  = (box.bean_class == getCurrentTargetBeanClass());
        beanInfo.push_back(info);
    }

    // ---- 构建 NumberBoxDisplayInfo 列表 ----
    std::vector<NumberBoxDisplayInfo> numberInfo;
    int targetBoxNum = getBoxNumberForBean(current_bean_class);
    for (const auto& box : current_number_boxes) {
        NumberBoxDisplayInfo info;
        info.box_number = box.box_number;
        info.position   = getBoxPosition(box.box_number);
        info.confidence = box.confidence;
        info.bbox       = box.bbox;
        info.label      = "data_" + std::to_string(box.box_number);
        info.is_target  = (box.box_number == targetBoxNum && current_bean_class >= 0);
        numberInfo.push_back(info);
    }

    // ---- 使用 Visualization 模块绘制检测框 ----
    drawDetectionBoxes(display, beanInfo, numberInfo);

    // ---- 状态文字叠加（模块不处理的特定信息） ----
    const char* states[] = {
        "SCAN_BEAN", "SCAN_BOX", "IDENTIFY_BEAN", "SEARCH_BOX", "WAIT", "IDLE"
    };

    // 状态 + 扫描进度（使用带填充背景的绘制）
    auto putInfo = [&](const std::string& text, const cv::Scalar& color, int yPos) {
        int baseline = 0;
        cv::Size textSz = cv::getTextSize(text, VizConfig::FONT,
                                          0.6, 2, &baseline);
        cv::Rect bg(0, yPos - textSz.height - 4,
                    textSz.width + 10, textSz.height + 8);
        cv::rectangle(display, bg, cv::Scalar(0, 0, 0), cv::FILLED);
        cv::putText(display, text, cv::Point(5, yPos),
                    VizConfig::FONT, 0.6, color, 2, cv::LINE_AA);
    };

    putInfo("State: " + std::string(states[current_state.load()]),
            cv::Scalar(0, 255, 255), 30);

    // 扫描进度
    if (current_state == STATE_SCAN_BEAN_AREA) {
        putInfo("Bean Mapping: " + std::to_string(scanned_bean_positions.size()) + "/3",
                cv::Scalar(255, 255, 0), 60);
    } else if (current_state == STATE_SCAN_BOX_AREA) {
        putInfo("Box Mapping: " + std::to_string(scanned_box_positions.size()) + "/5",
                cv::Scalar(255, 255, 0), 60);
    }

    // 当前目标信息（第 3 行）
    {
        int current_target = getCurrentTargetBeanClass();
        std::string target_info = "Current: ";
        if (current_target >= 0) {
            target_info += getBeanName(current_target);
            int pos = getBeanPosition(current_target);
            int tbox = getBoxNumberForBean(current_target);
            target_info += " → bean_pos:" + std::to_string(pos);
            target_info += " → box_" + std::to_string(tbox);
            target_info += " [" + std::to_string(placed_bean_count[current_target]) +
                           "/" + std::to_string(target_bean_count[current_target]) + "]";
        } else {
            target_info += "None (All done!)";
        }
        putInfo(target_info, cv::Scalar(255, 255, 0), 90);
    }

    cv::imshow("Robot Vision", display);
    cv::waitKey(1);
}
