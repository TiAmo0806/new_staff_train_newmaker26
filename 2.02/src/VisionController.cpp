/**
 * @file VisionController.cpp
 * @brief 视觉状态机实现 — 全自动比赛策略
 *
 * 这是整个 robot_vision 系统的核心调度文件。
 * 它实现了一个完整的状态机，控制着以下比赛流程：
 *
 * ┌─ 阶段 A：扫描建图（系统启动后只执行一次）────┐
 * │                                              │
 * │  STATE_WAIT_START                             │
 * │    ↓ 电控发 feedback=3                        │
 * │  STATE_SCAN_BEAN_AREA  ← 视觉识别 + 建映射表   │
 * │    ↓ 电控扫完发 feedback=3                    │
 * │  STATE_WAIT_BOX_SCAN   ← 等待电控转摄像头      │
 * │    ↓ 电控转好发 feedback=3                    │
 * │  STATE_SCAN_BOX_AREA   ← 视觉识别 + 建映射表   │
 * │    ↓ 电控扫完发 feedback=3                    │
 * │  → 进入任务阶段                               │
 * └──────────────────────────────────────────────┘
 *
 * ┌─ 阶段 B：取放循环（每颗豆子一轮）─────────────┐
 * │                                              │
 * │  STATE_IDENTIFY_BEAN → 发取豆区位置 (trk=0)   │
 * │    ↓ 电控到位发 feedback=3                    │
 * │  STATE_CONFIRM_POSITION → 视觉确认取豆位       │
 * │    ├─ ▲ 目标在视野 → 发锁定信号 (trk=1)        │
 * │    └─ ▼ 不在视野 → 重试≤3次，否则放弃          │
 * │    ↓ 电控抓完发 feedback=1                    │
 * │  STATE_SEARCH_BOX → 发放置区位置 (trk=0)      │
 * │    ↓ 电控到位发 feedback=3                    │
 * │  STATE_CONFIRM_BOX_POSITION → 视觉确认放置位    │
 * │    ├─ ▲ 目标在视野 → 发锁定信号 (trk=1)        │
 * │    └─ ▼ 不在视野 → 重试≤3次，否则放弃          │
 * │    ↓ 电控放置完发 feedback=2                  │
 * │  计数+1 → 循环到下一颗豆子                     │
 * │  → 全部完成 → STATE_IDLE                     │
 * └──────────────────────────────────────────────┘
 *
 * 通信协议:
 *   视觉→电控: 12B 包 (target_type + target_id + tracking + CRC)
 *   电控→视觉:  4B 包 (feedback_type + CRC)
 *
 * @see 最新的识别策略（项目根目录文档）
 * @see docs/communication_protocol.md
 * @see include/VisionController.hpp
 */

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
    // 打开串口（115200 8N1）
    serial = std::make_unique<SerialPort>(serial_port, 115200);
    if (!serial->isOpen()) {
        std::cerr << "[Controller] Failed to open serial port!" << std::endl;
    }

    // 重置计数
    for (int i = 0; i < 3; i++) {
        placed_bean_count[i] = 0;
    }
    current_order_index = 0;

    // 重置取豆区映射（-1 = 未映射）
    for (int i = 0; i < 3; i++) {
        bean_position_to_class[i] = -1;
    }
    bean_class_to_position.clear();
    scanned_bean_positions.clear();

    // 重置放置区映射（-1 = 未映射）
    for (int i = 0; i < 5; i++) {
        box_position_to_number[i] = -1;
    }
    box_number_to_position.clear();
    scanned_box_positions.clear();

    // ⭐ 打印启动信息
    printBanner("VISUAL SYSTEM READY — WAITING FOR MCU START SIGNAL (feedback=3)", '=');
    std::cout << "[Config] White kidney bean: " << BEAN_WHITE_KIDNEY_TARGET_COUNT << "x"
              << "  Soybean: " << BEAN_SOYBEAN_TARGET_COUNT << "x"
              << "  Mung bean: " << BEAN_MUNG_BEAN_TARGET_COUNT << "x" << std::endl;
    std::cout << "[Config] Order: white_kidney_bean → soybean → mung_bean" << std::endl;
    std::cout << "[Config] Serial port: " << serial_port << std::endl;

    // ⭐ 向电控发送启动握手信号
    //    视觉启动后立即主动发一个 0xA6 包给电控，告知"视觉已就绪"
    //    电控收到此包后启动自己的任务流程，然后发 feedback=3 回来确认
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
        return it->second;   // 返回对应的箱号 (1~3)
    }
    return -1;
}

// ============================================
// ⭐ 终端显示辅助
// ============================================

/**
 * @brief 打印用字符填充的分隔横幅，用于在终端标出重要状态变迁
 */
void VisionController::printBanner(const std::string& title, char fill) {
    std::string line(60, fill);
    std::cout << std::endl
              << line << std::endl
              << "  " << title << std::endl
              << line << std::endl;
}

/**
 * @brief 节流检查：限制终端打印频率为至少间隔 1 秒
 *
 * 防止 processFrame 在每帧都会调用 printStatusPanel 时输出刷屏。
 * 将耗时记录汇总到每秒输出一次的状态面板中。
 */
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
//
// 在终端显示当前状态、两个区域的映射进度、以及任务计数。
// 格式示例:
//   ──────────────────────────────────────────────
//     状态: 扫描取豆区 ■ 识别中...
//     取豆区(左→右): [soybean ] [mung_bean] [   ?    ]
//     放置区(左→右): [ - ] [ - ] [ - ] [ - ] [ - ]
//     当前: 环境扫描建图中...
//   ──────────────────────────────────────────────
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
    const char* sn = (s >= 0 && s <= STATE_IDLE) ? state_names[s] : "?";
    std::cout << "\n──────────────────────────────────────────────────────\n";
    std::cout << "  状态: " << sn << hint << "\n";

    // 取豆区映射
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

    // 放置区映射
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

    // 当前任务信息
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
// ⭐ 主循环 — 每帧调用一次
//
// 根据当前状态决定行为：
//   等待状态（WAIT_START, WAIT_ACTION, WAIT_BOX_SCAN,
//             WAIT_POSITION, WAIT_BOX_POSITION）
//     → 不推理，只检查串口反馈
//
//   空闲状态（IDLE）
//     → 只更新状态面板
//
//   识别状态（其他）
//     → 推理 + 分流到对应处理函数
// ============================================

void VisionController::processFrame(const cv::Mat& frame) {
    if (frame.empty()) return;

    // ⭐ 等待状态：不做推理，只检查电控反馈
    if (current_state == STATE_WAIT_START || current_state == STATE_WAIT_ACTION ||
        current_state == STATE_WAIT_BOX_SCAN || current_state == STATE_WAIT_POSITION ||
        current_state == STATE_WAIT_BOX_POSITION) {

        // ⭐ 启动握手重试：如果电控 2 秒内没回复，重发启动信号
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

    // ⭐ 执行视觉推理
    auto result = vision.infer(frame);

    // 提取当前帧中的检测框（按 X 坐标排序）
    extractBeanBoxes(result);
    extractNumberBoxes(result);

    // ⭐ 按当前状态分发到对应的处理函数
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

    // ⭐ 扫描阶段：推理完后同时检查电控反馈（电控可能随时发停止信号）
    if (current_state == STATE_SCAN_BEAN_AREA || current_state == STATE_SCAN_BOX_AREA) {
        checkMCUFeedback();
    }

    // 在调试画面上绘制检测结果
    drawDebugInfo(frame, result);
}

// ============================================
// ⭐ 检查电控反馈 — 状态机的核心驱动
//
// 电控通过串口发送 4 字节反馈包，feedback_type 决定如何进行状态跳转。
//
// feedback_type 与状态变迁:
//   1 (抓取完成) → STATE_SEARCH_BOX       抓取完成，找放置区位置
//   2 (放置完成) → 计数+1 → STATE_IDENTIFY_BEAN  放置完成，回到抓取循环
//   3 (移动完成) → 根据当前状态不同跳转：
//       WAIT_START             → SCAN_BEAN_AREA    启动握手成功
//       SCAN_BEAN_AREA         → WAIT_BOX_SCAN     取豆区扫完
//       WAIT_BOX_SCAN          → SCAN_BOX_AREA     转摄像头完成
//       SCAN_BOX_AREA          → IDENTIFY_BEAN     放置区扫完
//       WAIT_POSITION          → CONFIRM_POSITION  电控已到取豆位
//       WAIT_BOX_POSITION      → CONFIRM_BOX_POSITION 电控已到放置位
//   4 (动作失败) → 视觉忽略，电控自行重试
// ============================================

void VisionController::checkMCUFeedback() {
    if (!serial || !serial->isOpen()) return;

    rm_serial_driver::MCUToVisionPacket feedback;
    if (serial->receivePacket(feedback, 50)) {
        if (feedback.feedback_type == 1) {
            // ⭐ FEEDBACK_GRAB_DONE: 电控已执行完抓取动作
            current_state = STATE_SEARCH_BOX;
            printBanner("<< MCU: 抓取完成 → 找放置区箱子 >>", '=');

        } else if (feedback.feedback_type == 2) {
            // ⭐ FEEDBACK_PLACE_DONE: 电控已执行完放置动作
            //    对当前豆子计数 +1
            placed_bean_count[current_bean_class]++;

            if (isBeanTargetComplete(current_bean_class)) {
                // 当前豆子达到目标数量 → 跳到下一种
                advanceToNextBean();
                std::cout << "  ✓ " << current_bean_name << " 全部完成!" << std::endl;
            } else {
                // 还需继续抓同种豆子
                std::cout << "  " << current_bean_name << " (" << placed_bean_count[current_bean_class]
                          << "/" << target_bean_count[current_bean_class] << ")" << std::endl;
            }

            current_bean_class = -1;
            current_state = STATE_IDENTIFY_BEAN;
            printBanner("<< MCU: 放置完成 → 识别下一种豆子 >>", '=');

        } else if (feedback.feedback_type == 3) {
            // ⭐ FEEDBACK_MOVE_DONE: 电控移动完成 / 扫描阶段切换信号
            //    根据当前状态做不同的处理

            if (current_state == STATE_WAIT_START) {
                // 【启动握手】电控收到启动包后回复 → 开始扫描取豆区
                current_state = STATE_SCAN_BEAN_AREA;
                printBanner("<< 收到 MCU 启动信号 — 开始扫描取豆区 >>", '=');
                std::cout << "  请电控控制摄像头从左到右缓慢转动" << std::endl;

            } else if (current_state == STATE_WAIT_POSITION) {
                // 【取豆确认】电控已移动到取豆区目标位置 → 视觉确认
                current_state = STATE_CONFIRM_POSITION;
                std::cout << "  [确认] 电控已就位，视觉确认取豆目标位置..." << std::endl;

            } else if (current_state == STATE_WAIT_BOX_POSITION) {
                // 【放置确认】电控已移动到放置区目标位置 → 视觉确认
                current_state = STATE_CONFIRM_BOX_POSITION;
                std::cout << "  [确认] 电控已就位，视觉确认放置目标位置..." << std::endl;

            } else if (current_state == STATE_SCAN_BEAN_AREA) {
                // 【取豆区扫描结束】电控发停止信号
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
                // 【摄像头已转到放置区】电控发就位信号 → 开始扫描放置区
                current_state = STATE_SCAN_BOX_AREA;
                printBanner("<< 收到 MCU 启动信号 — 开始扫描放置区 >>", '=');
                std::cout << "  请电控控制摄像头从左到右缓慢转动" << std::endl;

            } else if (current_state == STATE_SCAN_BOX_AREA) {
                // 【放置区扫描结束】电控发停止信号
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
        // ⭐ feedback_type=4（动作失败）: 视觉不处理，由下位机自行重试
    }
}

// ============================================
// 提取取豆区豆子箱子信息
//
// 从推理结果中提取豆子检测框，过滤太小的噪声，
// 并按画面中的 X 坐标从左到右排序。
//
// 排序目的：后续 updateBeanMapping() 需要根据
// 画面中的左右顺序来推断位置编号。
// ============================================

void VisionController::extractBeanBoxes(const RobotVision::ClassificationResult& result) {
    current_bean_boxes.clear();

    for (const auto& det : result.beans) {
        // ⭐ 过滤太小的框（面积过于细小的很可能是噪声误识别）
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

    // 按画面 X 坐标排序（从左到右）
    std::sort(current_bean_boxes.begin(), current_bean_boxes.end(),
        [](const BeanBoxDetection& a, const BeanBoxDetection& b) {
            return a.center.x < b.center.x;
        });
}

// ============================================
// 提取放置区数字箱子信息
//
// 与 extractBeanBoxes 类似，从推理结果中提取数字标签，
// 过滤噪声后按 X 坐标排序。
// ============================================

void VisionController::extractNumberBoxes(const RobotVision::ClassificationResult& result) {
    current_number_boxes.clear();

    for (const auto& det : result.digits) {
        // 过滤太小的框
        if (det.bbox.width < 6 || det.bbox.height < 6) continue;

        NumberBoxDetection box;
        box.box_number = vision.getDigitValue(det.class_id);  // class_id=3→1, 4→2, ...
        box.bbox = det.bbox;
        box.center = det.center;
        box.confidence = det.confidence;
        current_number_boxes.push_back(box);
    }

    // 按画面 X 坐标排序（从左到右）
    std::sort(current_number_boxes.begin(), current_number_boxes.end(),
        [](const NumberBoxDetection& a, const NumberBoxDetection& b) {
            return a.center.x < b.center.x;
        });
}

// ============================================
// 阶段 1：扫描取豆区
//
// 电控控制摄像头从左到右缓慢转动，视觉不断做推理，
// 通过 updateBeanMapping() 增量建立取豆区 3 个位置的映射表。
//
// 映射完成后，视觉仍在等待电控发 feedback=3 来结束扫描。
// ============================================

void VisionController::handleScanBeanArea(const RobotVision::ClassificationResult& result) {
    updateBeanMapping();

    // 持续发送无目标信号（告诉电控"继续转动"）
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
// 更新取豆区映射表
//
// 核心逻辑：视觉通过画面中检测框的左右顺序，推断
// 每个豆子箱子在物理世界中的位置编号。
//
// 映射方法:
//   摄像头从取豆区左侧扫到右侧，画面中会逐个出现
//   不同的豆子箱子。已知一个已确定位置的箱子，就可以
//   根据画面中的左右顺序推断相邻箱子在物理世界中的位置。
//
//   具体策略:
//   1. 如果新豆子左右都有已映射的箱子 → 用坐标比例插值
//      计算位置（考虑镜头透视造成的视觉间距不均匀）
//   2. 如果只有左侧有已映射箱子 → 左侧位置 + 中间未映射数
//   3. 如果只有右侧有已映射箱子 → 右侧位置 - 中间未映射数
//   4. 如果左右都没有 → 假设为位置 0（第一个）
// ============================================

void VisionController::updateBeanMapping() {
    if (current_bean_boxes.empty()) return;

    // 遍历当前帧中按 X 坐标排序的豆子检测
    for (size_t i = 0; i < current_bean_boxes.size(); i++) {
        int bean_class = current_bean_boxes[i].bean_class;

        // 如果该豆子已经映射过，跳过
        if (bean_class_to_position.find(bean_class) != bean_class_to_position.end()) {
            continue;
        }

        int estimated_position = -1;
        int left_mapped_idx = -1;   // 左侧最近的已映射框在 current_bean_boxes 中的索引
        int right_mapped_idx = -1;  // 右侧最近的已映射框的索引

        // 查找左侧最近的已映射豆子
        int left_mapped_pos = -1;
        for (int j = (int)i - 1; j >= 0; j--) {
            int left_bean = current_bean_boxes[j].bean_class;
            if (bean_class_to_position.find(left_bean) != bean_class_to_position.end()) {
                left_mapped_pos = bean_class_to_position[left_bean];
                left_mapped_idx = j;
                break;
            }
        }

        // 查找右侧最近的已映射豆子
        int right_mapped_pos = -1;
        for (size_t j = i + 1; j < current_bean_boxes.size(); j++) {
            int right_bean = current_bean_boxes[j].bean_class;
            if (bean_class_to_position.find(right_bean) != bean_class_to_position.end()) {
                right_mapped_pos = bean_class_to_position[right_bean];
                right_mapped_idx = j;
                break;
            }
        }

        if (left_mapped_pos >= 0 && right_mapped_pos >= 0) {
            // ⭐ 左右都有参考：用坐标比例插值计算位置
            // 考虑摄像机透视效应：同样间距的物体在画面中可能左宽右窄
            // 因此用"画面坐标间距比例"来估算物理位置
            float left_x = current_bean_boxes[left_mapped_idx].center.x;
            float right_x = current_bean_boxes[right_mapped_idx].center.x;
            float current_x = current_bean_boxes[i].center.x;
            float ratio = (current_x - left_x) / (right_x - left_x);
            estimated_position = left_mapped_pos + (int)std::round(ratio * (right_mapped_pos - left_mapped_pos));
        } else if (left_mapped_pos >= 0) {
            // 只有左侧有参考：从左侧位置 + 中间跨越的未映射位置数
            int gap = 1;
            for (int j = (int)i - 1; j >= 0; j--) {
                int check_bean = current_bean_boxes[j].bean_class;
                if (bean_class_to_position.find(check_bean) == bean_class_to_position.end()) {
                    gap++;  // 中间还有未映射的豆子
                } else {
                    break;
                }
            }
            estimated_position = left_mapped_pos + gap;
        } else if (right_mapped_pos >= 0) {
            // 只有右侧有参考：从右侧位置 - 中间跨越的未映射位置数
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
            // 没有任何参考 → 假设为位置 0（第一个被看见的箱子）
            estimated_position = 0;
        }

        // 如果估算的位置有效且未被占用，写入映射表
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
// 获取取豆区位置（只读查询）
// ============================================

int VisionController::getBeanPosition(int bean_class) {
    auto it = bean_class_to_position.find(bean_class);
    if (it != bean_class_to_position.end()) {
        return it->second;
    }
    return -1;  // 未映射
}

// ============================================
// 判断豆子目标是否在视野中（只读查询）
//
// 遍历当前帧检测到的所有豆子框，检查是否存在
// 某个框的豆子类别对应的位置编号 = target_position。
// 不会修改映射表。
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
// 阶段 3：扫描放置区
//
// 与扫描取豆区对称。电控控制摄像头从左到右缓慢转动，
// 视觉不断做推理，通过 updateBoxMapping() 增量建立
// 放置区 5 个位置的数字→箱号映射表。
// ============================================

void VisionController::handleScanBoxArea(const RobotVision::ClassificationResult& result) {
    updateBoxMapping();

    sendVisionResult(TARGET_NONE, -1, false);

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
// 更新放置区映射表
//
// 逻辑与 updateBeanMapping 完全对称。
// 通过画面中数字标签的左右顺序推断位置编号。
// ============================================

void VisionController::updateBoxMapping() {
    if (current_number_boxes.empty()) return;

    for (size_t i = 0; i < current_number_boxes.size(); i++) {
        int box_num = current_number_boxes[i].box_number;

        if (box_number_to_position.find(box_num) != box_number_to_position.end()) {
            continue;
        }

        int estimated_position = -1;
        int left_mapped_idx = -1;
        int right_mapped_idx = -1;

        // 查找左侧最近的已映射箱子
        int left_mapped_pos = -1;
        for (int j = (int)i - 1; j >= 0; j--) {
            int left_box = current_number_boxes[j].box_number;
            if (box_number_to_position.find(left_box) != box_number_to_position.end()) {
                left_mapped_pos = box_number_to_position[left_box];
                left_mapped_idx = j;
                break;
            }
        }

        // 查找右侧最近的已映射箱子
        int right_mapped_pos = -1;
        for (size_t j = i + 1; j < current_number_boxes.size(); j++) {
            int right_box = current_number_boxes[j].box_number;
            if (box_number_to_position.find(right_box) != box_number_to_position.end()) {
                right_mapped_pos = box_number_to_position[right_box];
                right_mapped_idx = j;
                break;
            }
        }

        if (left_mapped_pos >= 0 && right_mapped_pos >= 0) {
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
// 获取放置区位置（只读查询）
// ============================================

int VisionController::getBoxPosition(int box_number) {
    auto it = box_number_to_position.find(box_number);
    if (it != box_number_to_position.end()) {
        return it->second;
    }
    return -1;
}

// ============================================
// 判断箱子目标是否在视野中（只读查询）
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
// 任务阶段：识别取豆区箱子位置
//
// 本函数在以下时序中被调用：
//   1. 查询当前需要抓取的豆子种类
//   2. 查取豆区映射表得到位置编号
//   3. 发送预定位信号给电控 (tracking=0)
//   4. 切换到 WAIT_POSITION 等待电控到位
//   5. 电控到位后由 checkMCUFeedback 切换到 CONFIRM_POSITION
// ============================================

void VisionController::handleIdentifyBean(const RobotVision::ClassificationResult& result) {
    if (isAllBeansComplete()) {
        // 所有豆子已完成
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

    // 查取豆区映射表，获取目标豆子的位置编号
    int target_position = getBeanPosition(target_bean_class);
    if (target_position < 0) {
        if (statusPrintReady())
            std::cout << "  [取豆] 错误: " << getBeanName(target_bean_class) << " 未映射!" << std::endl;
        sendVisionResult(TARGET_NONE, -1, false);
        return;
    }

    // ⭐ 设置当前目标信息，发送预定位信号（tracking=0）
    //    tracking=0 表示"请电控移动到取豆区第 X 个位置"
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
// ⭐ 电控到达取豆位置后，视觉确认目标
//
// 电控发 feedback=3 后由 checkMCUFeedback 触发进入本函数。
// 视觉做一次推理，检查目标位置的豆子是否在当前画面中。
//
// 结果:
//   目标在视野 → 发送锁定信号 (tracking=1) → 电控执行抓取
//   目标不在视野 → 重试（最多 3 次）
//     → 重试超限 → 放弃当前目标，回到识别状态
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

    if (isBeanTargetInView(target_position)) {
        // ✅ 目标在视野中 → 锁定并通知电控抓取
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
// ⭐ 电控到达放置位置后，视觉确认目标
//
// 与 handleConfirmPosition 对称，但检查的是放置区目标：
//   目标箱在视野 → 发送锁定信号 (tracking=1) → 电控放置
//   目标箱不在视野 → 重试（最多 3 次）
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

    if (isBoxTargetInView(target_position)) {
        // ✅ 目标箱在视野中 → 锁定并通知电控放置
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
// 任务阶段：找放置区箱子位置
//
// 电控完成抓取后进入本函数：
//   1. 查 BEAN_TO_BOX 映射表确定当前豆子对应哪个箱号
//   2. 查放置区映射表得到位置编号
//   3. 发送预定位信号给电控 (tracking=0)
//   4. 切换到 WAIT_BOX_POSITION 等待电控到位
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

    // 发放置区预定位信号（tracking=0）
    confirm_retry_count = 0;
    sendVisionResult(TARGET_BOX, target_position, false);

    printBanner("<< 请求放置: data_" + std::to_string(target_box_number)
                + " 在位置" + std::to_string(target_position)
                + " — 等待电控就位 >>", '=');

    current_state = STATE_WAIT_BOX_POSITION;
}

// ============================================
// 状态转换（保留备用，实际已由 checkMCUFeedback 驱动）
// ============================================

void VisionController::stateTransition() {
    // 状态转换已由 checkMCUFeedback() 中的 feedback 处理
    // 此函数保留用作手动转换的备用入口
}

// ============================================
// 发送数据包给电控
//
// 组装 VisionToMCUPacket 并通过串口发送。
// CRC 由 SerialPort::sendPacket 自动计算填充。
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

    serial->sendPacket(packet);
}

// ============================================
// 重置
//
// 将所有状态、映射表、计数器恢复为初始值，回到待启动状态。
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
//
// 在画面副本上绘制:
//   1. 检测框（豆子/数字箱带颜色标记）
//   2. 当前位置编号
//   3. 状态文字（当前状态 + 扫描进度 + 目标信息）
//
// 通过 cv::imshow 显示到"Robot Vision"窗口。
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

    // ---- 叠加状态文字 ----
    const char* states[] = {
        "WAIT_START", "SCAN_BEAN", "WAIT_BOX", "SCAN_BOX",
        "IDENTIFY_BEAN", "SEARCH_BOX", "WAIT_POS", "CONFIRM_POS",
        "WAIT_BOX_POS", "CONFIRM_BOX_POS",
        "WAIT", "IDLE"
    };

    // 带填充背景的文字绘制函数（方便阅读）
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

    // 第 1 行：状态
    putInfo("State: " + std::string(states[current_state.load()]),
            cv::Scalar(0, 255, 255), 30);

    // 第 2 行：扫描进度
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

    // 第 3 行：当前目标信息
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
