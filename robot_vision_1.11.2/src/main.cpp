/**
 * 实时视觉检测入口 —— 豆子与数字识别
 *
 * 使用 OpenVINO 加载 YOLOv8 ONNX 模型进行实时推理，
 * 调用 Visualization 模块绘制检测框和统计面板。
 *
 * 依赖:
 *   - RobotVision（视觉识别核心）
 *   - MindVisionCamera（工业相机驱动）
 *   - Visualization（绘制模块）
 *
 * 用法:
 *   直接运行 ./bin/robot_vision
 *   按 'q' 或 ESC 退出
 */

#include "../include/RobotVision.hpp"
#include "../include/MindVisionCamera.hpp"
#include "../include/Visualization.hpp"

#include <cstdlib>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <chrono>
#include <numeric>
#include <vector>

int main() {
    std::cout << "=== Robot Vision — Bean & Number Detector ===" << std::endl;

    // ---- 打开工业相机 ----
    MindVisionCamera camera;
    if (!camera.open(0)) {
        std::cerr << "Failed to open camera!" << std::endl;
        return -1;
    }

    // ---- 加载模型 ----
    const char* home = getenv("HOME");
    std::string model_path = home ? std::string(home) + "/robot_vision/models/best.onnx"
                                  : "../models/best.onnx";
    std::cout << "[Main] Loading model: " << model_path << std::endl;

    RobotVision vision(model_path);
    std::cout << "[Main] Model loaded! Starting detection loop..." << std::endl;

    // ---- FPS / 延迟统计（环形缓冲区，30 帧滑动窗口） ----
    constexpr size_t RING_SIZE = 30;
    std::vector<double> latencyRing(RING_SIZE, 0.0);
    size_t ringIdx = 0;

    // ---- 主循环 ----
    cv::Mat frame;
    int frameCount = 0;
    while (true) {
        // 1. 采集帧
        if (!camera.read(frame)) {
            std::cerr << "Failed to read frame!" << std::endl;
            break;
        }

        frameCount++;
        auto t0 = std::chrono::steady_clock::now();

        // 2. 推理
        auto result = vision.infer(frame);

        // 3. 统计检测数量
        int beanCount   = static_cast<int>(result.beans.size());
        int digitCount  = static_cast<int>(result.digits.size());

        // 4. 绘制检测框（使用可视化模块）
        drawDetections(frame, result);

        // 5. FPS / 延迟计算
        auto t1 = std::chrono::steady_clock::now();
        double latencyMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

        latencyRing[ringIdx % RING_SIZE] = latencyMs;
        ++ringIdx;
        double avgLatency = (ringIdx >= RING_SIZE)
            ? std::accumulate(latencyRing.begin(), latencyRing.end(), 0.0) / RING_SIZE
            : latencyMs;
        double fps = (avgLatency > 0.0) ? 1000.0 / avgLatency : 0.0;

        // 6. 绘制统计面板
        drawStats(frame, "RUNNING", beanCount, digitCount, fps, avgLatency);

        // 7. 显示
        cv::imshow("Robot Vision — Bean & Number Detector (q/ESC to quit)", frame);

        int key = cv::waitKey(1) & 0xFF;
        if (key == 'q' || key == 27) {  // q 或 ESC
            std::cout << "[Main] User quit." << std::endl;
            break;
        }

        // 每 15 帧打印一次摘要日志
        if (frameCount % 15 == 1) {
            std::cout << "[Frame " << frameCount << "] "
                      << "beans:" << beanCount
                      << " digits:" << digitCount
                      << "  |  ";
            for (const auto& det : result.beans) {
                std::cout << det.class_name << "(" << (int)(det.confidence*100) << "%) ";
            }
            for (const auto& det : result.target_digits) {
                std::cout << "data_" << vision.getDigitValue(det.class_id)
                          << "(" << (int)(det.confidence*100) << "%) ";
            }
            if (!result.ignore_digits.empty()) {
                std::cout << "(ignore:";
                for (const auto& det : result.ignore_digits) {
                    std::cout << "data_" << vision.getDigitValue(det.class_id)
                              << "(" << (int)(det.confidence*100) << "%) ";
                }
                std::cout << ")";
            }
            std::cout << "| FPS:" << fps << " Lat:" << (int)avgLatency << "ms" << std::endl;
        }
    }

    camera.close();
    cv::destroyAllWindows();
    std::cout << "[Main] Program ended." << std::endl;
    return 0;
}
