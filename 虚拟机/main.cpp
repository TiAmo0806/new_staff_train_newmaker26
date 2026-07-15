/**
 * 迈德威视相机 + OpenVINO YOLO11 实时目标检测 + 空间排序 + 双向串口通信
 *
 * 各模块职责：
 *   Camera        — 相机取帧
 *   preprocess    — 图像预处理
 *   Detector      — 模型推理 + 后处理
 *   SpatialSorter — 按 X 坐标从左到右排序
 *   VirtualSerial — 双向串口通信（TX: 发送排序结果, RX: 接收电控指令）
 *   visualize     — 绘制检测框
 *
 * 双向通信：
 *   TX（PC→MCU）：检测排序结果帧（0xA5 + count + class_ids + CRC16），每帧发送
 *   RX（MCU→PC）：控制指令帧（0x5A + action + CRC16），action: 0=停止, 1=开始
 *
 * main.cpp 只负责"串联调用"，不包含具体实现细节
 */

#include "camera.hpp"
#include "detector.hpp"
#include "spatial.hpp"
#include "visualize.hpp"
#include "VirtualSerial.h"

#include <opencv2/opencv.hpp>
#include <chrono>
#include <stdio.h>

int main(int argc, char** argv) {
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
    std::string modelPath = (argc > 1) ? argv[1] : "./best.xml";
    printf("[INFO] 模型路径: %s\n", modelPath.c_str());

    Detector detector;
    if (!detector.load(modelPath)) {
        fprintf(stderr, "[ERROR] 模型加载失败\n");
        return -1;
    }

    // ============================================================
    // 3. 初始化串口通信（向电控发送检测排序结果）
    // ============================================================
    VirtualSerial serial;
    serial.SetTxLogEnabled(true);   // 终端打印发送的帧数据（调试用）
    // serial.SetSimulated(true);   // 无串口时启用模拟模式

    if (!serial.Open()) {
        fprintf(stderr, "[WARN] 串口打开失败，将以模拟模式运行\n");
        serial.SetSimulated(true);
    }

    // ============================================================
    // 3b. 设置双向通信 RX 回调（电控 → PC）
    //     电控发来的包: 0x5A + action(0/1) + CRC16
    //     action: 0=停止采集, 1=开始采集
    // ============================================================
    bool captureEnabled = true;    // 默认开启采集推理
    int  lastRxAction  = -1;       // 最近收到的电控指令（-1=尚未收到, 0=停止, 1=开始）
    int  rxFrameAge    = 999;      // 距离上次收到电控信号的帧数

    serial.SetRxCallback([&](uint8_t action) {
        lastRxAction = action;
        rxFrameAge   = 0;

        if (action == 1) {
            captureEnabled = true;
            printf("[CTRL] 电控指令：开始采集 (action=%d)\n", action);
        } else if (action == 0) {
            captureEnabled = false;
            printf("[CTRL] 电控指令：停止采集 (action=%d)\n", action);
        } else {
            // 非法值：保持当前状态，打印警告
            printf("[CTRL] 警告：收到未知指令 action=%d，已忽略（当前状态不变）\n", action);
        }
    });

    // ============================================================
    // 4. 主循环
    // ============================================================
    auto lastTime = std::chrono::steady_clock::now();
    int  frameCount = 0;
    float fps = 0.0f;
    cv::Mat frame;

    printf("\n[INFO] 开始实时检测，按 ESC 退出\n\n");

    while (cam.read(frame)) {
        // ---------- 4a. 接收电控指令（双向通信） ----------
        serial.PollReceive();

        // ---------- 4b. 灰度相机转 BGR ----------
        cv::Mat display;
        if (cam.isMono()) {
            cv::cvtColor(frame, display, cv::COLOR_GRAY2BGR);
        } else {
            display = frame.clone();
        }

        // ---------- 4c. 推理 + 排序 + 发送（受电控 CTRL 指令控制） ----------
        std::vector<Detection> detections;
        std::vector<Detection> sorted;
        std::vector<ObjectCenter> centers;
        std::string orderStr;

        if (captureEnabled) {
            // ---- 推理 ----
            detections = detector.detect(frame);

            // ---- 按 X 坐标从左到右排序 ----
            sorted    = SpatialSorter::sortLeftToRight(detections);
            centers   = SpatialSorter::sortedCenters(detections);
            orderStr  = SpatialSorter::formatOrder(sorted);

            // ---- 打印 + 串口发送排序结果 ----
            // 始终发送（即使没有检测到物品也发空帧），让 MCU 知道工控机在正常运行
            {
                std::vector<int> classIds;
                if (!sorted.empty()) {
                    printf("[排序] 从左到右: %s\n", orderStr.c_str());
                    classIds.reserve(sorted.size());
                    for (const auto& d : sorted)
                        classIds.push_back(d.class_id);
                }
                serial.sendDetectionOrder(classIds);  // 空 vector → count=0 空帧
            }

        }

        // ---------- 4d. 绘制（有检测结果时才绘制框） ----------
        if (!sorted.empty()) {
            drawDetections(display, sorted);     // 检测框 + 标签
            drawCenters(display, centers);       // 中心点 + 序号
        }

        // ---------- 4e. FPS 统计 ----------
        frameCount++;
        rxFrameAge++;
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - lastTime).count();
        if (elapsed >= 1.0f) {
            fps = frameCount / elapsed;
            lastTime = now;
            frameCount = 0;
        }

        // ---------- 4f. 顶部信息栏 ----------
        char info[128];
        if (captureEnabled) {
            snprintf(info, sizeof(info),
                     "FPS: %.1f | Objects: %zu | Order: %s | Mode: MCU_CTRL",
                     fps, detections.size(), orderStr.c_str());
            cv::putText(display, info, cv::Point(10, 30),
                        cv::FONT_HERSHEY_SIMPLEX, 0.6,
                        cv::Scalar(0, 255, 0), 2);
        } else {
            // 暂停模式：显示等待信息
            cv::putText(display, "WAITING FOR MCU (CTRL: 1 to start)",
                        cv::Point(10, 30),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7,
                        cv::Scalar(0, 0, 255), 2);
            snprintf(info, sizeof(info), "FPS: %.1f | Status: PAUSED", fps);
            cv::putText(display, info, cv::Point(10, 60),
                        cv::FONT_HERSHEY_SIMPLEX, 0.5,
                        cv::Scalar(0, 0, 255), 1);
        }

        // ---------- 4g. RX 信号指示灯（右上角） ----------
        {
            int cx = display.cols - 120;  // 指示灯区域右边缘
            int cy = 30;                   // 指示灯区域上边缘

            // 指示灯颜色：绿=开始, 红=停止, 黄=未知指令, 灰=未收到信号
            cv::Scalar lightColor;
            const char* rxLabel;
            if (lastRxAction == 1) {
                lightColor = cv::Scalar(0, 255, 0);    // 绿色：开始采集
                rxLabel = "RX: START";
            } else if (lastRxAction == 0) {
                lightColor = cv::Scalar(0, 0, 255);    // 红色：停止采集
                rxLabel = "RX: STOP";
            } else if (lastRxAction < 0) {
                lightColor = cv::Scalar(128, 128, 128); // 灰色：尚未收到
                rxLabel = "RX: --";
            } else {
                lightColor = cv::Scalar(0, 220, 255);   // 黄色：未知指令
                rxLabel = "RX: ??";                      // 如电控发了 3
            }

            // 实心圆点（指示灯）
            cv::circle(display, cv::Point(cx, cy), 8, lightColor, -1);
            // 外圈
            cv::circle(display, cv::Point(cx, cy), 9, cv::Scalar(200, 200, 200), 1);

            // 文字标签
            cv::putText(display, rxLabel, cv::Point(cx + 15, cy + 5),
                        cv::FONT_HERSHEY_SIMPLEX, 0.5, lightColor, 2);

            // 信号年龄（多久前收到的）
            char ageStr[32];
            if (lastRxAction < 0) {
                snprintf(ageStr, sizeof(ageStr), "无信号");
            } else if (lastRxAction > 1) {
                snprintf(ageStr, sizeof(ageStr), "非法值:%d", lastRxAction);
            } else {
                snprintf(ageStr, sizeof(ageStr), "%d帧前", rxFrameAge);
            }
            cv::putText(display, ageStr, cv::Point(cx + 15, cy + 22),
                        cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(180, 180, 180), 1);
        }

        // ---------- 4h. 显示 ----------
        cv::imshow("Real-time Detection (ESC to exit)", display);

        // ---------- 4h. 按键 ----------
        if (cv::waitKey(1) == 27) {   // ESC
            printf("[INFO] ESC 按下，退出\n");
            break;
        }
    }

    // ============================================================
    // 5. 清理
    // ============================================================
    serial.Close();
    cam.release();
    cv::destroyAllWindows();
    printf("[INFO] 程序正常退出\n");
    return 0;
}
