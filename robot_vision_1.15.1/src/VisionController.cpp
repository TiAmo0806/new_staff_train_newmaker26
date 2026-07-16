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
    : vision(model_path), last_status_print(std::chrono::steady_clock::now()) {
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

    printBanner("VISUAL SYSTEM READY — WAITING FOR MCU START SIGNAL (feedback=3)", '=');
    std::cout << "[Config] White kidney bean: " << BEAN_WHITE_KIDNEY_TARGET_COUNT << "x"
              << "  Soybean: " << BEAN_SOYBEAN_TARGET_COUNT << "x"
              << "  Mung bean: " << BEAN_MUNG_BEAN_TARGET_COUNT << "x" << std::endl;
    std::cout << "[Config] Order: white_kidney_bean → soybean → mung_bean" << std::endl;
    std::cout << "[Config] Serial port: " << serial_port << std::endl;

    // ⭐ 向电控发送启动握手信号，"视觉已就绪，请开始你的程序"
    //    电控收到此包后启动自己的任务流程，然后发 feedback=3 回来
    printBanner(">> 发送启动握手信号到电控... <<", '-');
    sendVisionResult(TARGET_NONE, 0, false);
    startup_signal_sent = true;
    last_startup_signal_time = std::chrono::steady_clock::now();
    std::cout << "[Controller] 启动信号已发送，等待电控回复 (feedback=3)..." << std::endl;
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
// ⭐ 终端显示辅助
// ============================================

void VisionController::printBanner(const std::string& title, char fill) {
    std::string line(60, fill);
    std::cout << std::endl
              << line << std::endl
              << "  " << title << std::endl
              << line << std::endl;
}

bool VisionController::statusPrintReady() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_status_print).count();
    if (elapsed >= 1000) {
        last_status_print = now;
        return true;
    }
    return false;
}

// ============================================
// ⭐ 状态面板（节流到 1 秒一次，不刷屏）
// ============================================

void VisionController::printStatusPanel() {
    // 节流：至少间隔 1 秒
    if (!statusPrintReady()) return;

    static const char* state_names[] = {
        "等待启动",      // STATE_WAIT_START
        "扫描取豆区",    // STATE_SCAN_BEAN_AREA
        "等待转摄像头",  // STATE_WAIT_BOX_SCAN
        "扫描放置区",    // STATE_SCAN_BOX_AREA
        "识别取豆",      // STATE_IDENTIFY_BEAN
        "寻找放置区",    // STATE_SEARCH_BOX
        "等待到位(取豆)",// STATE_WAIT_POSITION
        "确认取豆位",    // STATE_CONFIRM_POSITION
        "等待到位(放置)",// STATE_WAIT_BOX_POSITION
        "确认放置位",    // STATE_CONFIRM_BOX_POSITION
        "等待执行",      // STATE_WAIT_ACTION
        "任务完成",      // STATE_IDLE
    };

    RobotState s = current_state.load();

    // ---- 识别中？ ----
    bool is_recognizing = (s == STATE_SCAN_BEAN_AREA || s == STATE_SCAN_BOX_AREA ||
                           s == STATE_IDENTIFY_BEAN || s == STATE_CONFIRM_POSITION ||
                           s == STATE_CONFIRM_BOX_POSITION);
    std::string hint = is_recognizing ? " ■ 识别中..." : "";

    // ---- 组装面板内容 ----
    // 第 1 行：状态
    const char* sn = (s >= 0 && s <= STATE_IDLE) ? state_names[s] : "?";
    std::cout << "\n──────────────────────────────────────────────────────\n";
    std::cout << "  状态: " << sn << hint << "\n";

    // 第 2 行：取豆区映射
    std::cout << "  取豆区(左→右):";
    for (int i = 0; i < 3; i++) {
        if (bean_position_to_class[i] >= 0)
            std::cout << " [" << std::setw(8) << std::left
                      << getBeanName(bean_position_to_class[i]) << "]";
        else
            std::cout << " [   ?    ]";
    }
    if (isAllBeanPositionsMapped())
        std::cout << "  ✓";
    std::cout << "\n";

    // 第 3 行：放置区映射
    std::cout << "  放置区(左→右):";
    for (int i = 0; i < 5; i++) {
        if (box_position_to_number[i] >= 0)
            std::cout << " [箱" << box_position_to_number[i] << "]";
        else
            std::cout << " [ - ]";
    }
    if (isAllBoxPositionsMapped())
        std::cout << "  ✓";
    std::cout << "\n";

    // 第 4 行：当前任务
    if (current_bean_class >= 0) {
        int pc = placed_bean_count[current_bean_class];
        int tc = target_bean_count[current_bean_class];
        int next = getCurrentTargetBeanClass();

        std::cout << "  当前: " << current_bean_name
                  << "  [" << pc << "/" << tc << "]";

        if (next >= 0 && next != current_bean_class)
            std::cout << "  下一颗: " << getBeanName(next);

        if (pc >= tc) std::cout << "  ✓ 已完成";
        std::cout << "\n";
    } else if (s == STATE_SCAN_BEAN_AREA || s == STATE_SCAN_BOX_AREA ||
               s == STATE_WAIT_BOX_SCAN) {
        std::cout << "  当前: 环境扫描建图中...\n";
    } else if (s == STATE_IDLE) {
        std::cout << "  当前: ★★★ 全 部 完 成 ! ★★★\n";
    } else {
        std::cout << "  当前: 就绪\n";
    }

    std::cout << "──────────────────────────────────────────────────────\n"
              << std::endl;
}

// ============================================
// ⭐ 主循环
// ============================================

void VisionController::processFrame(const cv::Mat& frame) {
    if (frame.empty()) return;

    // ⭐ 等待状态的帧：只检查电控反馈，不做推理
    if (current_state == STATE_WAIT_START || current_state == STATE_WAIT_ACTION ||
        current_state == STATE_WAIT_BOX_SCAN || current_state == STATE_WAIT_POSITION ||
        current_state == STATE_WAIT_BOX_POSITION) {

        // ⭐ WAIT_START: 如果电控长时间没回复，重发启动握手信号
        if (current_state == STATE_WAIT_START && startup_signal_sent) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - last_startup_signal_time).count();
            if (elapsed >= 2) {
                last_startup_signal_time = now;
                sendVisionResult(TARGET_NONE, 0, false);
                std::cout << "[Controller] 重发启动信号..." << std::endl;
            }
        }

        checkMCUFeedback();
        printStatusPanel();
        return;
    }

    if (current_state == STATE_IDLE) {
        printStatusPanel();
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
        case STATE_CONFIRM_POSITION:
            handleConfirmPosition(result);
            break;
        case STATE_CONFIRM_BOX_POSITION:
            handleConfirmBoxPosition(result);
            break;
        case STATE_SEARCH_BOX:
            handleSearchBox(result);
            break;
        default:
            break;
    }

    // ⭐ 状态面板（节流 1 秒）
    printStatusPanel();

    // ⭐ 扫描阶段：推理完后检查电控是否有 "停止扫描" 信号
    if (current_state == STATE_SCAN_BEAN_AREA || current_state == STATE_SCAN_BOX_AREA) {
        checkMCUFeedback();
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
            printBanner("<< MCU: 抓取完成 → 找放置区箱子 >>", '=');
        } else if (feedback.feedback_type == 2) {
            // 放置完成 → 计数，回到识别状态
            placed_bean_count[current_bean_class]++;

            if (isBeanTargetComplete(current_bean_class)) {
                advanceToNextBean();
                std::cout << "  ✓ " << current_bean_name << " 全部完成!" << std::endl;
            } else {
                std::cout << "  " << current_bean_name << " (" << placed_bean_count[current_bean_class]
                          << "/" << target_bean_count[current_bean_class] << ")" << std::endl;
            }

            current_bean_class = -1;
            current_state = STATE_IDENTIFY_BEAN;
            printBanner("<< MCU: 放置完成 → 识别下一种豆子 >>", '=');
        } else if (feedback.feedback_type == 3) {
            // 移动完成 / 扫描启动/停止信号
            if (current_state == STATE_WAIT_START) {
                // 收到启动信号 → 开始扫描取豆区
                current_state = STATE_SCAN_BEAN_AREA;
                printBanner("<< 收到 MCU 启动信号 — 开始扫描取豆区 >>", '=');
                std::cout << "  请电控控制摄像头从左到右缓慢转动" << std::endl;

            } else if (current_state == STATE_WAIT_POSITION) {
                // ⭐ 电控到达取豆目标位置 → 视觉确认
                current_state = STATE_CONFIRM_POSITION;
                std::cout << "  [确认] 电控已就位，视觉确认取豆目标位置..." << std::endl;

            } else if (current_state == STATE_WAIT_BOX_POSITION) {
                // ⭐ 电控到达放置目标位置 → 视觉确认
                current_state = STATE_CONFIRM_BOX_POSITION;
                std::cout << "  [确认] 电控已就位，视觉确认放置目标位置..." << std::endl;

            } else if (current_state == STATE_SCAN_BEAN_AREA) {
                // 取豆区扫完 → 停止识别，等待电控转摄像头到放置区
                if (isAllBeanPositionsMapped()) {
                    printBeanMappingTable();
                } else {
                    std::cout << "[WARN] 取豆区扫描结束，但映射不完整!" << std::endl;
                    std::cout << "  未映射位置: ";
                    for (int i = 0; i < 3; i++)
                        if (bean_position_to_class[i] == -1) std::cout << "位置" << i << " ";
                    std::cout << std::endl;
                }
                current_state = STATE_WAIT_BOX_SCAN;
                printBanner("<< 取豆区扫描结束 — 等待 MCU 转摄像头到放置区 >>", '=');

            } else if (current_state == STATE_WAIT_BOX_SCAN) {
                // 电控已转好摄像头到放置区方向 → 开始扫描放置区
                current_state = STATE_SCAN_BOX_AREA;
                printBanner("<< 收到 MCU 启动信号 — 开始扫描放置区 >>", '=');
                std::cout << "  请电控控制摄像头从左到右缓慢转动" << std::endl;

            } else if (current_state == STATE_SCAN_BOX_AREA) {
                // 放置区扫完 → 进入任务执行
                if (isAllBoxPositionsMapped()) {
                    printBoxMappingTable();
                } else {
                    std::cout << "[WARN] 放置区扫描结束，但映射不完整!" << std::endl;
                    std::cout << "  未映射位置: ";
                    for (int i = 0; i < 5; i++)
                        if (box_position_to_number[i] == -1) std::cout << "位置" << i << " ";
                    std::cout << std::endl;
                }
                current_state = STATE_IDENTIFY_BEAN;
                printBanner("<< 放置区扫描结束 — 开始执行任务! >>", '=');

            } else {
                std::cout << "[State] MCU 移动完成" << std::endl;
            }
        }
        // ⭐ feedback=4（动作失败）不处理，由下位机自行重试
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
    updateBeanMapping();

    sendVisionResult(TARGET_NONE, -1, false);

    // 每 1 秒打印一次状态，避免刷屏
    if (!statusPrintReady()) return;

    if (isAllBeanPositionsMapped()) {
        std::cout << "  [取豆区] 已全部识别! 等待 MCU 发信号结束扫描..." << std::endl;
    } else {
        std::cout << "  [取豆区] 已映射 " << scanned_bean_positions.size() << "/3 → ";
        for (int i = 0; i < 3; i++) {
            if (bean_position_to_class[i] >= 0)
                std::cout << getBeanName(bean_position_to_class[i]) << " ";
            else
                std::cout << "[空] ";
        }
        std::cout << "| 继续转动中..." << std::endl;
    }
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
    std::string line(52, '=');
    std::cout << std::endl << line << std::endl;
    std::cout << "  【取豆区映射表】从左到右：" << std::endl;
    std::cout << line << std::endl;
    for (int i = 0; i < 3; i++) {
        int bean = bean_position_to_class[i];
        std::string name = (bean >= 0) ? getBeanName(bean) : "?";
        std::cout << "  |  位置 " << i << "  →  " << name;
        if (i < 2) std::cout << std::endl;
    }
    std::cout << std::endl << line << std::endl;
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
    updateBoxMapping();

    sendVisionResult(TARGET_NONE, -1, false);

    // 每 1 秒打印一次状态，避免刷屏
    if (!statusPrintReady()) return;

    if (isAllBoxPositionsMapped()) {
        std::cout << "  [放置区] 已全部识别! 等待 MCU 发信号结束扫描..." << std::endl;
    } else {
        std::cout << "  [放置区] 已映射 " << scanned_box_positions.size() << "/5 → ";
        for (int i = 0; i < 5; i++) {
            if (box_position_to_number[i] >= 0)
                std::cout << box_position_to_number[i] << " ";
            else
                std::cout << "[空] ";
        }
        std::cout << "| 继续转动中..." << std::endl;
    }
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
    std::string line(52, '=');
    std::cout << std::endl << line << std::endl;
    std::cout << "  【放置区映射表】从左到右：" << std::endl;
    std::cout << line << std::endl;
    for (int i = 0; i < 5; i++) {
        int box = box_position_to_number[i];
        std::string name = (box >= 0) ? "data_" + std::to_string(box) : "?";
        std::cout << "  |  位置 " << i << "  →  " << name;
        if (i < 4) std::cout << std::endl;
    }
    std::cout << std::endl << line << std::endl;
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
        printBanner("★★★ 任务完成！所有豆子已放置完毕！ ★★★", '*');
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
        if (statusPrintReady())
            std::cout << "  [取豆] 错误: " << getBeanName(target_bean_class) << " 未映射!" << std::endl;
        sendVisionResult(TARGET_NONE, -1, false);
        return;
    }

    // ⭐ 新流程：设置目标信息，发送位置信号给电控（不锁定）
    // tracking=0 表示"请移动到取豆区第X个位置"
    current_bean_class = target_bean_class;
    current_bean_name = getBeanName(target_bean_class);
    sendVisionResult(TARGET_BEAN_BOX, target_position, false);

    confirm_retry_count = 0;

    printBanner("<< 请求取豆: " + current_bean_name + " 在位置" + std::to_string(target_position)
                + " — 等待电控就位 >>", '=');
    std::cout << "  进度: " << placed_bean_count[current_bean_class]
              << "/" << target_bean_count[current_bean_class] << std::endl;

    current_state = STATE_WAIT_POSITION;
}

// ============================================
// ⭐ 新增：电控到达位置后，视觉确认目标
// ============================================

void VisionController::handleConfirmPosition(const RobotVision::ClassificationResult& result) {
    if (current_bean_class < 0) {
        current_state = STATE_IDENTIFY_BEAN;
        return;
    }

    int target_position = getBeanPosition(current_bean_class);
    if (target_position < 0) {
        std::cout << "[Error] " << getBeanName(current_bean_class) << " 位置未映射!" << std::endl;
        current_state = STATE_IDENTIFY_BEAN;
        return;
    }

    // 查取豆区映射表 → 当前豆子在取豆区从左到右第几个位置
    // 检查当前摄像头画面中是否包含这个位置
    if (isBeanTargetInView(target_position)) {
        // ✅ 确认目标在视野中 → 锁定并通知电控抓取
        sendVisionResult(TARGET_BEAN_BOX, target_position, true);
        printBanner("<< 锁定取豆目标: " + current_bean_name + " 在位置" + std::to_string(target_position) + " >>", '=');
        std::cout << "  进度: " << placed_bean_count[current_bean_class]
                  << "/" << target_bean_count[current_bean_class] << std::endl;
        current_state = STATE_WAIT_ACTION;
    } else {
        // ❌ 目标不在视野中 → 重试
        confirm_retry_count++;
        if (confirm_retry_count >= 3) {
            std::cout << "[Error] 无法确认 " << current_bean_name
                      << " 在位置" << target_position << " — 放弃!" << std::endl;
            sendVisionResult(TARGET_NONE, -1, false);
            current_state = STATE_IDENTIFY_BEAN;
            confirm_retry_count = 0;
        } else {
            std::cout << "[Retry " << confirm_retry_count << "/3] " << current_bean_name
                      << " 不在视野中，重新请求电控就位..." << std::endl;
            sendVisionResult(TARGET_BEAN_BOX, target_position, false);
            current_state = STATE_WAIT_POSITION;
        }
    }
}

// ============================================
// ⭐ 新增：电控到达放置位置后，视觉确认目标
// ============================================

void VisionController::handleConfirmBoxPosition(const RobotVision::ClassificationResult& result) {
    if (current_bean_class < 0) {
        current_state = STATE_SEARCH_BOX;
        return;
    }

    int target_box_number = getBoxNumberForBean(current_bean_class);
    if (target_box_number < 0) {
        std::cout << "[Error] data_" << target_box_number << " 箱号未映射!" << std::endl;
        current_state = STATE_SEARCH_BOX;
        return;
    }

    int target_position = getBoxPosition(target_box_number);
    if (target_position < 0) {
        std::cout << "[Error] data_" << target_box_number << " 位置未映射!" << std::endl;
        current_state = STATE_SEARCH_BOX;
        return;
    }

    // 查放置区映射表 → 目标箱的位置编号
    // 检查当前摄像头画面中是否包含这个位置
    if (isBoxTargetInView(target_position)) {
        // ✅ 确认目标箱在视野中 → 锁定并通知电控放置
        sendVisionResult(TARGET_BOX, target_position, true);
        printBanner("<< 锁定放置目标: data_" + std::to_string(target_box_number)
                    + " 在位置" + std::to_string(target_position) + " >>", '=');
        current_state = STATE_WAIT_ACTION;
    } else {
        // ❌ 目标不在视野中 → 重试
        confirm_retry_count++;
        if (confirm_retry_count >= 3) {
            std::cout << "[Error] 无法确认 data_" << target_box_number
                      << " 在位置" << target_position << " — 放弃!" << std::endl;
            sendVisionResult(TARGET_NONE, -1, false);
            current_state = STATE_SEARCH_BOX;
            confirm_retry_count = 0;
        } else {
            std::cout << "[Retry " << confirm_retry_count << "/3] data_" << target_box_number
                      << " 不在视野中，重新请求电控就位..." << std::endl;
            sendVisionResult(TARGET_BOX, target_position, false);
            current_state = STATE_WAIT_BOX_POSITION;
        }
    }
}

// ============================================
// 状态4：找放置区箱子位置（正常任务）
// ⭐ 新流程：发送位置信号 → 等待电控到位 → 视觉确认
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
        if (statusPrintReady())
            std::cout << "  [放置] 错误: data_" << target_box_number << " 未映射!" << std::endl;
        return;
    }

    // ⭐ 新流程：发送位置信号给电控（tracking=0）→ 等电控停到位
    confirm_retry_count = 0;
    sendVisionResult(TARGET_BOX, target_position, false);

    printBanner("<< 请求放置: data_" + std::to_string(target_box_number)
                + " 在位置" + std::to_string(target_position)
                + " — 等待电控就位 >>", '=');

    current_state = STATE_WAIT_BOX_POSITION;
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
    // CRC 由 sendPacket 自动计算

    serial->sendPacket(packet);
}

// ============================================
// 重置
// ============================================

void VisionController::reset() {
    current_state = STATE_WAIT_START;
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

    confirm_retry_count = 0;

    for (int i = 0; i < 5; i++) {
        box_position_to_number[i] = -1;
    }
    box_number_to_position.clear();
    scanned_box_positions.clear();

    std::cout << "[Controller] Reset" << std::endl;
    printBanner("SYSTEM RESET — WAITING FOR MCU START SIGNAL (feedback=3)", '=');
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
        "WAIT_START", "SCAN_BEAN", "WAIT_BOX", "SCAN_BOX",
        "IDENTIFY_BEAN", "SEARCH_BOX", "WAIT_POS", "CONFIRM_POS",
        "WAIT_BOX_POS", "CONFIRM_BOX_POS",
        "WAIT", "IDLE"
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
    } else if (current_state == STATE_WAIT_BOX_SCAN) {
        putInfo("Bean scan done, waiting for MCU to turn camera to box area...",
                cv::Scalar(0, 255, 0), 60);
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
