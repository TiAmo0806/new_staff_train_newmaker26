/**
 * 迈德威视相机 + OpenVINO YOLO11 实时目标检测 + 空间排序 + 串口发送
 *
 * 各模块职责：
 *   Camera        — 相机取帧
 *   preprocess    — 图像预处理
 *   Detector      — 模型推理 + 后处理
 *   SpatialSorter — 按 X 坐标从左到右排序
 *   VirtualSerial — 串口发送排序结果给电控
 *   visualize     — 绘制检测框
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
    // 4. 主循环
    // ============================================================
    auto lastTime = std::chrono::steady_clock::now();
    int  frameCount = 0;
    float fps = 0.0f;
    cv::Mat frame;

    printf("\n[INFO] 开始实时检测，按 ESC 退出\n\n");

    while (cam.read(frame)) {
        // ---------- 4a. 灰度相机转 BGR ----------
        cv::Mat display;
        if (cam.isMono()) {
            cv::cvtColor(frame, display, cv::COLOR_GRAY2BGR);
        } else {
            display = frame.clone();
        }

        // ---------- 4b. 模型推理 ----------
        auto detections = detector.detect(frame);

        // ---------- 4c. 按 X 坐标从左到右排序 ----------
        auto sorted    = SpatialSorter::sortLeftToRight(detections);
        auto centers   = SpatialSorter::sortedCenters(detections);
        std::string orderStr = SpatialSorter::formatOrder(sorted);

        // ---------- 4d. 打印 + 串口发送排序结果 ----------
        if (!sorted.empty()) {
            printf("[排序] 从左到右: %s\n", orderStr.c_str());

            // 提取 class_id 列表，发送给电控
            std::vector<int> classIds;
            classIds.reserve(sorted.size());
            for (const auto& d : sorted)
                classIds.push_back(d.class_id);
            serial.sendDetectionOrder(classIds);
        }

        // ---------- 4e. 绘制 ----------
        drawDetections(display, sorted);     // 检测框 + 标签
        drawCenters(display, centers);       // 中心点 + 序号

        // ---------- 4f. FPS 统计 ----------
        frameCount++;
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - lastTime).count();
        if (elapsed >= 1.0f) {
            fps = frameCount / elapsed;
            lastTime = now;
            frameCount = 0;
        }

        // ---------- 4g. 顶部信息栏 ----------
        char info[128];
        snprintf(info, sizeof(info), "FPS: %.1f | Objects: %zu | Order: %s",
                 fps, detections.size(), orderStr.c_str());
        cv::putText(display, info, cv::Point(10, 30),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6,
                    cv::Scalar(0, 255, 0), 2);

        // ---------- 4h. 显示 ----------
        cv::imshow("Real-time Detection (ESC to exit)", display);

        // ---------- 4i. 按键 ----------
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
