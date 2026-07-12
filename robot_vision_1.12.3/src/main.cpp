/**
 * 实时视觉检测入口 —— 豆子与数字识别（带串口通信）
 *
 * 使用 OpenVINO 加载 YOLOv8 ONNX 模型进行实时推理，
 * 通过 VisionController 管理完整状态机并与 C 板（STM32）串口通信。
 *
 * 流程:
 *   WAIT_START → SCAN_BEAN → WAIT_BOX_SCAN → SCAN_BOX
 *   → IDENTIFY_BEAN → WAIT_POSITION → CONFIRM_POSITION
 *   → SEARCH_BOX → WAIT_BOX_POSITION → CONFIRM_BOX_POSITION → ...
 *
 * 用法:
 *   ./bin/robot_vision [串口设备路径]
 *   默认串口: /dev/ttyACM0
 *   按 'q' 或 ESC 退出
 *
 * 依赖:
 *   - VisionController（封装了状态机 + 视觉推理 + 串口通信）
 *   - MindVisionCamera（工业相机驱动）
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

    signal(SIGINT, signalHandler);

    // ---- 参数解析 ----
    const char* home = getenv("HOME");
    std::string serial_port = (argc > 1) ? argv[1] : "/dev/ttyACM0";
    std::string model_path = home
        ? std::string(home) + "/robot_vision/models/best.onnx"
        : "../models/best.onnx";

    std::cout << "[Main] Model:       " << model_path << std::endl;
    std::cout << "[Main] Serial port: " << serial_port << std::endl;
    std::cout << "[Main] Baud rate:   115200 (8N1)" << std::endl;

    // ---- 打开工业相机 ----
    MindVisionCamera camera;
    if (!camera.open(0)) {
        std::cerr << "[Main] Failed to open camera!" << std::endl;
        return -1;
    }
    std::cout << "[Main] Camera opened." << std::endl;

    // ---- 创建 VisionController（状态机 + 视觉推理 + 串口） ----
    //     构造函数内部自动打开串口，失败会打印错误但不阻塞启动
    VisionController controller(model_path, serial_port);

    // ---- 主循环 ----
    //     VisionController::processFrame 内部处理：
    //       1. 状态机调度
    //       2. YOLO 推理（非等待状态下）
    //       3. 串口收发（sendVisionResult / checkMCUFeedback）
    //       4. 画面绘制（drawDebugInfo → imshow）
    cv::Mat frame;
    while (g_running) {
        if (!camera.read(frame)) {
            std::cerr << "[Main] Failed to read frame!" << std::endl;
            break;
        }

        controller.processFrame(frame);

        // 检查退出键
        int key = cv::waitKey(1) & 0xFF;
        if (key == 'q' || key == 27) {
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
