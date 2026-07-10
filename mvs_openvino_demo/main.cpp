/**
 * 迈德威视相机 + OpenVINO YOLO11 实时目标检测
 *
 * 各模块职责：
 *   Camera      — 相机取帧
 *   preprocess  — 图像预处理
 *   Detector    — 模型推理 + 后处理
 *   visualize   — 绘制检测框
 *
 * main.cpp 只负责"串联调用"，不包含具体实现细节
 */

#include "src/camera.hpp"
#include "src/detector.hpp"
#include "src/visualize.hpp"

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
    // 可选：调整检测参数
    // detector.confThreshold = 0.5f;
    // detector.nmsThreshold  = 0.4f;
    if (!detector.load(modelPath)) {
        fprintf(stderr, "[ERROR] 模型加载失败\n");
        return -1;
    }

    // ============================================================
    // 3. 主循环
    // ============================================================
    auto lastTime = std::chrono::steady_clock::now();
    int  frameCount = 0;
    float fps = 0.0f;
    cv::Mat frame;

    printf("\n[INFO] 开始实时检测，按 ESC 退出\n\n");

    while (cam.read(frame)) {
        // ---------- 3a. 处理灰度相机 ----------
        cv::Mat display;
        if (cam.isMono()) {
            cv::cvtColor(frame, display, cv::COLOR_GRAY2BGR);
        } else {
            display = frame.clone();
        }

        // ---------- 3b. 推理 ----------
        auto detections = detector.detect(frame);

        // ---------- 3c. 绘制 ----------
        drawDetections(display, detections);

        // ---------- 3d. FPS 统计 ----------
        frameCount++;
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - lastTime).count();
        if (elapsed >= 1.0f) {
            fps = frameCount / elapsed;
            lastTime = now;
            frameCount = 0;
        }

        // ---------- 3e. 显示信息 ----------
        char info[128];
        snprintf(info, sizeof(info), "FPS: %.1f | Objects: %zu",
                 fps, detections.size());
        cv::putText(display, info, cv::Point(10, 30),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7,
                    cv::Scalar(0, 255, 0), 2);

        // ---------- 3f. 显示 ----------
        cv::imshow("Real-time Detection (ESC to exit)", display);

        // ---------- 3g. 按键 ----------
        if (cv::waitKey(1) == 27) {   // ESC
            printf("[INFO] ESC 按下，退出\n");
            break;
        }
    }

    // ============================================================
    // 4. 清理
    // ============================================================
    cam.release();
    cv::destroyAllWindows();
    printf("[INFO] 程序正常退出\n");
    return 0;
}
