/**
 * @file main.cpp
 * @brief 实时视觉检测主入口 —— 全自动"抓豆子-放箱子"比赛程序
 *
 * 完整运行流程:
 *   1. 打开迈德威视工业相机
 *   2. 加载 YOLOv8 ONNX 模型
 *   3. 创建 VisionController（自动打开串口 + 发送启动握手信号）
 *   4. 主循环：采集帧 → processFrame（状态机自动调度）
 *   5. 整个比赛流程在状态机内自动执行：
 *      扫描建图（取豆区→放置区）→ 取放循环 → 任务完成
 *
 * 用法:
 *   ./bin/robot_vision [串口设备路径]
 *   默认串口: /dev/ttyACM0
 *   按 'q' 或 ESC 退出
 *
 * @see include/VisionController.hpp
 * @see include/MindVisionCamera.hpp
 */

#include "../include/VisionController.hpp"
#include "../include/MindVisionCamera.hpp"

#include <iostream>
#include <cstdlib>
#include <csignal>

static volatile bool g_running = true;

void signalHandler(int) {
    g_running = false;
}

int main(int argc, char* argv[]) {
    std::cout << "=== Robot Vision — Bean & Number Detector ===" << std::endl;
    std::cout << "       (with Serial Communication)" << std::endl;

    signal(SIGINT, signalHandler);  // Ctrl+C 优雅退出

    // ---- 参数解析 ----
    const char* home = getenv("HOME");
    std::string serial_port = (argc > 1) ? argv[1] : "/dev/ttyACM0";  // 默认串口
    std::string model_path = home
        ? std::string(home) + "/robot_vision/models/best.onnx"
        : "../models/best.onnx";

    std::cout << "[Main] Model:       " << model_path << std::endl;
    std::cout << "[Main] Serial port: " << serial_port << std::endl;
    std::cout << "[Main] Baud rate:   115200 (8N1)" << std::endl;

    // ---- 打开迈德威视工业相机 ----
    MindVisionCamera camera;
    if (!camera.open(0)) {
        std::cerr << "[Main] Failed to open camera!" << std::endl;
        return -1;
    }
    std::cout << "[Main] Camera opened." << std::endl;

    // ---- 创建 VisionController（状态机 + 视觉推理 + 串口通信） ----
    //     构造函数内部:
    //       1. 加载 ONNX 模型
    //       2. 打开串口（失败会打印错误但不阻塞启动）
    //       3. 发送启动握手信号给电控
    VisionController controller(model_path, serial_port);

    // ---- 主循环 ----
    cv::Mat frame;
    while (g_running) {
        if (!camera.read(frame)) {
            std::cerr << "[Main] Failed to read frame!" << std::endl;
            break;
        }

        // VisionController::processFrame 内部自动处理:
        //   1. 状态机调度（根据当前状态决定做什么）
        //   2. YOLO 推理（非等待状态下执行）
        //   3. 串口收发（sendVisionResult / checkMCUFeedback）
        //   4. 画面绘制（drawDebugInfo → imshow）
        controller.processFrame(frame);

        int key = cv::waitKey(1);
        if (key == 'q' || key == 27) {  // 'q' 或 ESC 退出
            std::cout << "[Main] User quit." << std::endl;
            break;
        }
    }

    camera.close();
    controller.reset();
    cv::destroyAllWindows();
    std::cout << "[Main] Program ended." << std::endl;
    return 0;
}
