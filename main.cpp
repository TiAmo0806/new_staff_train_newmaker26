/**
 * 迈德威视相机 + OpenVINO YOLO11 实时目标检测 + 空间排序 + 单向串口通信
 *
 * 各模块职责：
 *   Camera        — 相机取帧
 *   Detector      — 模型推理 + 后处理
 *   SpatialSorter — 按 X 坐标从左到右排序
 *   VirtualSerial — 单向串口通信（依次发送数字包 + 豆子包）
 *   visualize     — 绘制检测框
 *
 * 通信流程（单向双包）：
 *   扫描数字(30帧) → 发送数字包 → 间隔(15帧) → 扫描豆子(30帧) → 发送豆子包 → 结束
 *
 * 互斥规则：每帧检测结果中豆子与数字不可混合
 * 推理补全：4 个数字 → 推理第 5 个；2 个豆子 → 推理第 3 个
 *
 * main.cpp 只负责"串联调用"，不包含具体实现细节
 */

#include "camera.hpp"
#include "config.hpp"
#include "detector.hpp"
#include "spatial.hpp"
#include "visualize.hpp"
#include "VirtualSerial.h"
#include "packet.hpp"

#include <opencv2/opencv.hpp>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <set>
#include <unordered_set>

// ============================================================
// 通信状态机
// ============================================================
enum class CommPhase {
    SCANNING_DIGITS,   // 扫描数字帧（最多 30 帧）
    DIGIT_GAP,         // 数字包已发，等待 15 帧间隔
    SCANNING_BEANS,    // 扫描豆子帧（最多 30 帧）
    DONE               // 双包已发，通信结束
};

static constexpr int DIGIT_SCAN_FRAMES = 30;
static constexpr int BEAN_SCAN_FRAMES  = 30;
static constexpr int GAP_FRAMES        = 15;

// ============================================================
// 辅助函数
// ============================================================

/// 从排序后的检测结果提取去重后的 class_id（保持 X 坐标排序）
static std::vector<int> extractUniqueClassIds(const std::vector<Detection>& sorted) {
    std::vector<int> ids;
    std::unordered_set<int> seen;
    for (const auto& d : sorted) {
        if (seen.insert(d.class_id).second)
            ids.push_back(d.class_id);
    }
    return ids;
}

/// 检查是否全是豆子（class_id ∈ {0,1,2}）
static bool isPureBeans(const std::vector<Detection>& dets) {
    if (dets.empty()) return false;
    for (const auto& d : dets)
        if (!isBeanClass(d.class_id)) return false;
    return true;
}

/// 检查是否全是数字（class_id ∈ {3,4,5,6,7}）
static bool isPureDigits(const std::vector<Detection>& dets) {
    if (dets.empty()) return false;
    for (const auto& d : dets)
        if (!isDigitClass(d.class_id)) return false;
    return true;
}

/// 找出缺失的数字（给定去重后的数字列表，返回 ALL_DIGIT_CLASSES 中缺失的那个）
static int findMissingDigit(const std::vector<int>& present) {
    std::set<int> have(present.begin(), present.end());
    for (int d : ALL_DIGIT_CLASSES) {
        if (have.find(d) == have.end())
            return d;
    }
    return -1;  // 不应该到这里（5 个全齐）
}

/// 找出缺失的豆子（给定去重后的豆子列表，返回 ALL_BEAN_CLASSES 中缺失的那个）
static int findMissingBean(const std::vector<int>& present) {
    std::set<int> have(present.begin(), present.end());
    for (int b : ALL_BEAN_CLASSES) {
        if (have.find(b) == have.end())
            return b;
    }
    return -1;  // 不应该到这里（3 个全齐）
}

/// 获取阶段名称（用于 UI 显示）
static const char* phaseName(CommPhase phase) {
    switch (phase) {
        case CommPhase::SCANNING_DIGITS: return "SCAN_DIGITS";
        case CommPhase::DIGIT_GAP:       return "GAP_WAIT";
        case CommPhase::SCANNING_BEANS:  return "SCAN_BEANS";
        case CommPhase::DONE:            return "DONE";
    }
    return "???";
}

// ============================================================
// 主入口
// ============================================================
int main(int argc, char** argv) {
    // ============================================================
    // 0. 加载配置文件
    // ============================================================
    Config cfg;
    std::string configPath = (argc > 2) ? argv[2] : "config.yaml";
    if (!cfg.loadFromYAML(configPath)) {
        printf("[Config] 使用内置默认参数\n");
    }

    // ============================================================
    // 1. 打开相机
    // ============================================================
    Camera cam;
    if (!cam.open()) {
        fprintf(stderr, "[ERROR] 相机初始化失败\n");
        return -1;
    }

    // ============================================================
    // 2. 加载模型
    // ============================================================
    std::string modelPath = (argc > 1) ? argv[1] : cfg.modelPath;
    printf("[INFO] 模型路径: %s\n", modelPath.c_str());

    Detector detector;
    detector.confThreshold = cfg.confThreshold;
    detector.nmsThreshold  = cfg.nmsThreshold;
    detector.inputWidth    = cfg.inputWidth;
    detector.inputHeight   = cfg.inputHeight;

    if (!detector.load(modelPath, cfg.device)) {
        fprintf(stderr, "[ERROR] 模型加载失败\n");
        return -1;
    }

    // ============================================================
    // 3. 初始化串口通信（单向 TX：数字包 + 豆子包）
    // ============================================================
    VirtualSerial serial(cfg.serialPort);
    serial.SetTxLogEnabled(cfg.txLogEnabled);
    serial.SetAutoReconnect(cfg.autoReconnect);

    if (!serial.Open()) {
        fprintf(stderr, "[WARN] 串口打开失败，将以模拟模式运行\n");
        serial.SetSimulated(true);
    }

    // ============================================================
    // 4. 通信状态机初始化
    // ============================================================
    CommPhase phase = CommPhase::SCANNING_DIGITS;
    int frameCounter = 0;       // 当前阶段帧计数
    bool digitSent   = false;
    bool beanSent    = false;

    printf("\n[COMM] 通信流程开始\n");
    printf("[COMM] Phase 1: 扫描数字 (%d 帧) → 发送数字包\n", DIGIT_SCAN_FRAMES);
    printf("[COMM] Phase 2: 间隔等待 (%d 帧)\n", GAP_FRAMES);
    printf("[COMM] Phase 3: 扫描豆子 (%d 帧) → 发送豆子包\n", BEAN_SCAN_FRAMES);
    printf("[COMM] Phase 4: 结束通信\n\n");

    // ============================================================
    // 5. 主循环
    // ============================================================
    auto lastTime = std::chrono::steady_clock::now();
    int  frameCount = 0;
    float fps = 0.0f;
    cv::Mat frame;

    printf("[INFO] 开始实时检测，按 ESC 退出\n\n");

    while (cam.read(frame)) {
        // ---------- 5a. 灰度相机转 BGR ----------
        cv::Mat display;
        if (cam.isMono()) {
            cv::cvtColor(frame, display, cv::COLOR_GRAY2BGR);
        } else {
            display = frame.clone();
        }

        // ---------- 5b. 推理 + 排序 ----------
        auto detections = detector.detect(frame);
        auto sorted     = SpatialSorter::sortLeftToRight(detections);
        auto centers    = SpatialSorter::sortedCenters(detections);
        auto orderStr   = SpatialSorter::formatOrder(sorted);

        // 去重 class_id（保持 X 排序）
        auto uniqueIds  = extractUniqueClassIds(sorted);

        // ---------- 5c. 通信状态机 ----------
        switch (phase) {

        case CommPhase::SCANNING_DIGITS: {
            // 只处理纯数字帧（豆子+数字混合 → 作废）
            if (isPureDigits(sorted) && uniqueIds.size() >= 4) {
                // 取前 4 个实际检测到的数字（按 X 排序）
                std::vector<int> digitIds(uniqueIds.begin(),
                                          uniqueIds.begin() + std::min<size_t>(4, uniqueIds.size()));

                // 推理补全缺失的第 5 个数字
                int missing = findMissingDigit(digitIds);
                if (missing >= 0) {
                    digitIds.push_back(missing);
                }

                // 发送数字包
                if (serial.sendDigitPacket(digitIds)) {
                    digitSent = true;
                    printf("[COMM] ✓ 数字包已发送: ");
                    for (size_t i = 0; i < digitIds.size(); ++i) {
                        if (i > 0) printf(" ");
                        printf("%d", digitIds[i]);
                    }
                    printf("  (帧计数=%d)\n", frameCounter);
                    phase = CommPhase::DIGIT_GAP;
                    frameCounter = 0;
                }
                break;
            }

            // 30 帧内未找到有效数字帧
            frameCounter++;
            if (frameCounter >= DIGIT_SCAN_FRAMES) {
                printf("[COMM] ⚠ 数字扫描超时 (%d 帧)，跳过数字包\n", DIGIT_SCAN_FRAMES);
                phase = CommPhase::DIGIT_GAP;
                frameCounter = 0;
            }
            break;
        }

        case CommPhase::DIGIT_GAP: {
            frameCounter++;
            if (frameCounter >= GAP_FRAMES) {
                printf("[COMM] → 间隔结束，开始扫描豆子\n");
                phase = CommPhase::SCANNING_BEANS;
                frameCounter = 0;
            }
            break;
        }

        case CommPhase::SCANNING_BEANS: {
            // 只处理纯豆子帧（豆子+数字混合 → 作废）
            if (isPureBeans(sorted) && uniqueIds.size() >= 2) {
                // 取前 2 个实际检测到的豆子（按 X 排序）
                std::vector<int> beanIds(uniqueIds.begin(),
                                         uniqueIds.begin() + std::min<size_t>(2, uniqueIds.size()));

                // 推理补全缺失的第 3 个豆子
                int missing = findMissingBean(beanIds);
                if (missing >= 0) {
                    beanIds.push_back(missing);
                }

                // 发送豆子包
                if (serial.sendBeanPacket(beanIds)) {
                    beanSent = true;
                    printf("[COMM] ✓ 豆子包已发送: ");
                    for (size_t i = 0; i < beanIds.size(); ++i) {
                        if (i > 0) printf(" ");
                        printf("%d", beanIds[i]);
                    }
                    printf("  (帧计数=%d)\n", frameCounter);
                    phase = CommPhase::DONE;
                    serial.Close();
                    printf("[COMM] 双包发送完毕，通信结束\n");
                }
                break;
            }

            // 30 帧内未找到有效豆子帧
            frameCounter++;
            if (frameCounter >= BEAN_SCAN_FRAMES) {
                printf("[COMM] ⚠ 豆子扫描超时 (%d 帧)，跳过豆子包\n", BEAN_SCAN_FRAMES);
                phase = CommPhase::DONE;
                serial.Close();
                printf("[COMM] 通信结束（豆子包未发送）\n");
            }
            break;
        }

        case CommPhase::DONE:
            // 通信已结束，继续显示画面但不再发送
            break;
        }

        // ---------- 5d. 绘制 ----------
        if (!sorted.empty()) {
            drawDetections(display, sorted);
            drawCenters(display, centers);
        }

        // ---------- 5e. FPS 统计 ----------
        frameCount++;
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - lastTime).count();
        if (elapsed >= cfg.fpsInterval) {
            fps = frameCount / elapsed;
            lastTime = now;
            frameCount = 0;
        }

        // ---------- 5f. 顶部信息栏 ----------
        {
            // 行 1: 阶段 + 帧计数 + 状态
            char line1[160];
            const char* statusIcon = "○";
            if (phase == CommPhase::DONE) {
                statusIcon = (digitSent && beanSent) ? "●" : "◐";
            }
            snprintf(line1, sizeof(line1),
                     "Phase: %s | Frame: %d | FPS: %.1f | Objects: %zu | Order: %s",
                     phaseName(phase), frameCounter, fps,
                     detections.size(), orderStr.c_str());
            cv::putText(display, line1, cv::Point(10, 30),
                        cv::FONT_HERSHEY_SIMPLEX, 0.5,
                        cv::Scalar(0, 255, 0), 2);

            // 行 2: 包发送状态
            char line2[128];
            snprintf(line2, sizeof(line2),
                     "DIGIT: %s | BEAN: %s",
                     digitSent ? "SENT" : "----",
                     beanSent  ? "SENT" : "----");
            cv::putText(display, line2, cv::Point(10, 52),
                        cv::FONT_HERSHEY_SIMPLEX, 0.45,
                        cv::Scalar(200, 200, 200), 1);

            // 互斥规则提示（混合帧）
            if (!sorted.empty() && !isPureBeans(sorted) && !isPureDigits(sorted)) {
                cv::putText(display, "MIXED (SKIPPED)",
                            cv::Point(10, 72),
                            cv::FONT_HERSHEY_SIMPLEX, 0.45,
                            cv::Scalar(0, 0, 255), 1);
            }

            // 通信结束提示
            if (phase == CommPhase::DONE) {
                cv::putText(display, "COMM DONE",
                            cv::Point(display.cols - 150, 30),
                            cv::FONT_HERSHEY_SIMPLEX, 0.7,
                            cv::Scalar(0, 255, 255), 2);
            }
        }

        // ---------- 5g. 显示 ----------
        cv::imshow(cfg.windowName, display);

        // ---------- 5h. 按键 ----------
        if (cv::waitKey(1) == 27) {   // ESC
            printf("[INFO] ESC 按下，退出\n");
            break;
        }
    }

    // ============================================================
    // 6. 清理
    // ============================================================
    detector.waitAll();
    serial.Close();
    cam.release();
    cv::destroyAllWindows();
    printf("[INFO] 程序正常退出\n");
    return 0;
}
