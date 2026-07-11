/**
 * 迈德威视相机 + OpenVINO YOLO11 实时目标检测 + 空间位置分析
 *
 * 各模块职责：
 *   Camera      — 相机取帧
 *   preprocess  — 图像预处理
 *   Detector    — 模型推理 + 后处理
 *   SpatialAnalyzer — 空间位置关系分析
 *   visualize   — 绘制检测框
 *
 * main.cpp 只负责"串联调用"，不包含具体实现细节
 */

#include "camera.hpp"
#include "detector.hpp"
#include "spatial.hpp"
#include "visualize.hpp"

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
    // 3. 空间分析器（可调容差）
    // ============================================================
    SpatialAnalyzer spatial;
    spatial.xTolerance = 50.0f;   // X 方向容差（像素），小于此值视为同一列
    spatial.yTolerance = 50.0f;   // Y 方向容差（像素），小于此值视为同一行

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

        // ---------- 4c. 空间位置分析 ----------
        auto centers   = spatial.extractCenters(detections);  // 提取中心点 + 4角
        auto relations = spatial.analyze(detections);          // 分析两两关系

        // ---------- 4d. 打印到控制台 ----------
        if (relations.size() >= 2) {
            printf("--- 位置关系 (%zu 个对象) ---\n", centers.size());
            for (size_t i = 0; i < centers.size(); ++i) {
                printf("  [%zu] %s: 4角(%.0f,%.0f  %.0f,%.0f  %.0f,%.0f  %.0f,%.0f)  中心(%.0f, %.0f)\n",
                       i, centers[i].class_name.c_str(),
                       centers[i].x1, centers[i].y1,
                       centers[i].x2, centers[i].y1,
                       centers[i].x1, centers[i].y2,
                       centers[i].x2, centers[i].y2,
                       centers[i].cx, centers[i].cy);
            }
            for (const auto& rel : relations) {
                printf("  %s\n", SpatialAnalyzer::format(rel).c_str());
            }
            printf("---------------------------\n");
        }

        // ---------- 4e. 绘制 ----------
        drawDetections(display, detections);   // 检测框 + 标签
        drawCenters(display, centers, relations); // 中心点 + 关系连线

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
        snprintf(info, sizeof(info), "FPS: %.1f | Objects: %zu | Pairs: %zu",
                 fps, detections.size(), relations.size());
        cv::putText(display, info, cv::Point(10, 30),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7,
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
    cam.release();
    cv::destroyAllWindows();
    printf("[INFO] 程序正常退出\n");
    return 0;
}
