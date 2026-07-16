/**
 * test_recognition.cpp —— 独立识别测试程序
 *
 * 功能：打开相机 → 加载模型 → 实时推理 → 在窗口中显示检测结果
 * 临时使用，脱离状态机和串口，方便测试模型识别效果。
 *
 * 用法:
 *   ./bin/test_recognition               # 迈德威视工业相机（默认）
 *   ./bin/test_recognition --usb         # USB 摄像头
 *   ./bin/test_recognition --file img.jpg  # 单张图片/视频文件
 *   按 'q' 或 ESC 退出
 */

#include "../include/RobotVision.hpp"
#include "../include/MindVisionCamera.hpp"
#include "../include/Visualization.hpp"

#include <opencv2/opencv.hpp>
#include <iostream>
#include <cctype>
#include <csignal>
#include <chrono>

static volatile bool g_running = true;
void signalHandler(int) { g_running = false; }

// 类别名称 → 显示文本（英文 + 数字，OpenCV Hershey 字体不支持中文）
static const std::map<std::string, std::string> ZH_NAMES = {
    {"soybean",           "soybean"},
    {"mung_bean",         "mung_bean"},
    {"white_kidney_bean", "white_kidney_bean"},
    {"data_1", "1"},
    {"data_2", "2"},
    {"data_3", "3"},
    {"data_4", "4"},
    {"data_5", "5"},
};

// 从 frame 读取一帧（统一封装工业相机 / VideoCapture）
static bool readFrame(cv::Mat& frame, MindVisionCamera* cam, cv::VideoCapture* cap) {
    if (cam) return cam->read(frame);
    if (cap) { *cap >> frame; return !frame.empty(); }
    return false;
}

int main(int argc, char* argv[]) {
    std::cout << "=== Robot Vision — 识别测试模式 ===" << std::endl;
    std::cout << "  显示相机画面，框出目标物体并标注标签" << std::endl;
    std::cout << "  按 'q' 或 ESC 退出" << std::endl;
    std::cout << std::endl;

    signal(SIGINT, signalHandler);

    const char* home = getenv("HOME");
    std::string model_path = home
        ? std::string(home) + "/robot_vision/models/best.onnx"
        : "../models/best.onnx";

    // ---- 参数解析 ----
    bool use_usb = false;
    std::string file_source;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--usb") use_usb = true;
        else if (arg == "--file" && i + 1 < argc) file_source = argv[++i];
        else if (arg == "--help" || arg == "-h") {
            std::cout << "用法:\n"
                      << "  ./bin/test_recognition                # 迈德威视工业相机\n"
                      << "  ./bin/test_recognition --usb          # USB 摄像头\n"
                      << "  ./bin/test_recognition --file <path>  # 图片/视频文件\n";
            return 0;
        }
    }

    // ---- 打开视频源 ----
    cv::VideoCapture cap;
    MindVisionCamera camera;
    MindVisionCamera* cam_ptr = nullptr;
    cv::VideoCapture* cap_ptr = nullptr;

    if (!file_source.empty()) {
        cap.open(file_source);
        if (!cap.isOpened()) {
            std::cerr << "[Error] 无法打开文件: " << file_source << std::endl;
            return -1;
        }
        std::cout << "[OK] 已打开文件: " << file_source << std::endl;
        cap_ptr = &cap;
    } else if (use_usb) {
        cap.open(0);
        if (!cap.isOpened()) {
            std::cerr << "[Error] 无法打开 USB 摄像头 /dev/video0" << std::endl;
            return -1;
        }
        std::cout << "[OK] USB 摄像头已打开." << std::endl;
        cap_ptr = &cap;
    } else {
        if (camera.open(0)) {
            cam_ptr = &camera;
            std::cout << "[OK] 迈德威视相机已打开." << std::endl;
        } else {
            std::cerr << "[Error] 迈德威视相机未找到!" << std::endl;
            std::cout << "\n提示: 可尝试以下模式:\n"
                      << "  ./bin/test_recognition --usb               # USB 摄像头\n"
                      << "  ./bin/test_recognition --file <path>       # 图片/视频文件\n";
            return -1;
        }
    }

    // ---- 加载模型 ----
    RobotVision vision(model_path, 0.3f);

    // ---- 主循环 ----
    cv::Mat frame;
    auto last_time = std::chrono::steady_clock::now();
    int frame_count = 0;
    double fps = 0.0;

    while (g_running) {
        if (!readFrame(frame, cam_ptr, cap_ptr)) {
            if (cam_ptr) {
                std::cerr << "[Error] 读取相机帧失败!" << std::endl;
                break;
            } else {
                // 视频文件播放完毕
                std::cout << "[Info] 播放结束." << std::endl;
                break;
            }
        }

        // FPS 计算
        frame_count++;
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - last_time).count();
        if (elapsed >= 1.0) {
            fps = frame_count / elapsed;
            frame_count = 0;
            last_time = now;
        }

        // 推理 + 耗时
        auto infer_start = std::chrono::steady_clock::now();
        auto result = vision.infer(frame);
        auto infer_end = std::chrono::steady_clock::now();
        double latency = std::chrono::duration<double, std::milli>(infer_end - infer_start).count();

        // ⭐ 在画面副本上绘制检测结果
        cv::Mat display = frame.clone();
        drawDetections(display, result);

        // ⭐ 添加英文标签（在检测框旁补充名称）
        for (const auto& det : result.beans) {
            auto it = ZH_NAMES.find(det.class_name);
            if (it != ZH_NAMES.end()) {
                cv::putText(display,
                            it->second,
                            cv::Point(det.bbox.x + 2, det.bbox.y + det.bbox.height / 2),
                            VizConfig::FONT, 0.5, cv::Scalar(0, 255, 255), 2, cv::LINE_AA);
            }
        }
        for (const auto& det : result.digits) {
            auto it = ZH_NAMES.find(det.class_name);
            if (it != ZH_NAMES.end()) {
                cv::putText(display,
                            "Box " + it->second,
                            cv::Point(det.bbox.x + 2, det.bbox.y + det.bbox.height / 2),
                            VizConfig::FONT, 0.5, cv::Scalar(0, 255, 255), 2, cv::LINE_AA);
            }
        }

        // 统计面板
        drawStats(display,
                  "Test Mode",
                  static_cast<int>(result.beans.size()),
                  static_cast<int>(result.digits.size()),
                  fps, latency);

        // 显示窗口
        cv::imshow("Recognition Test - Robot Vision", display);

        int key = cv::waitKey(1) & 0xFF;
        if (key == 'q' || key == 27) break;
    }

    // ⭐ 单张图片模式下，等待按键再退出
    auto isImageExt = [](const std::string& path) {
        size_t dot = path.find_last_of('.');
        if (dot == std::string::npos) return false;
        std::string ext;
        for (char c : path.substr(dot)) ext += (char)std::tolower((unsigned char)c);
        return ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp";
    };
    if (!file_source.empty() && isImageExt(file_source)) {
        std::cout << "[提示] 单张图片，按任意键退出..." << std::endl;
        cv::waitKey(0);
    }

    if (cam_ptr) camera.close();
    if (cap_ptr) cap.release();
    cv::destroyAllWindows();
    std::cout << "[Done] 测试结束." << std::endl;
    return 0;
}
