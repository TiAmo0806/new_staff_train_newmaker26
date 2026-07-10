/**
 * vision.cpp —— NUC 视觉主程序
 *
 * ── 数据流 ──
 *   相机 → YOLO推理 → 稳定跟踪 → 查表 → 串口发送
 *
 * ── 容错 ──
 *   模型/串口失败不退出，照常显示相机画面
 */

#include <iostream>
#include <chrono>
#include <thread>
#include <filesystem>
#include <opencv2/opencv.hpp>

#include "config.hpp"
#include "cemare.hpp"
#include "visualizer.hpp"
#include "yolo_detector.hpp"
#include "route_config.hpp"
#include "stable_tracker.hpp"
#include "packet.hpp"
#include "CRC16.hpp"
#include "serial.hpp"

namespace fs = std::filesystem;

int main()
{
    std::cout << "NUC 视觉节点启动" << std::endl;

    // 配置 
    std::string visionCfgPath = resolveProjectPath("vision_config.yaml");
    std::string routeCfgPath  = resolveProjectPath("route_config.txt");

    VisionConfig cfg = loadVisionConfig(visionCfgPath);
    RouteConfig  routes;  routes.load(routeCfgPath);
    auto lastVisionMtime = safeLastWriteTime(visionCfgPath);
    auto colors = buildColorTable(CLASS_NAMES.size());

    // 模型（可降级） 
    YOLODetector detector;
    bool modelOk = detector.loadModel(resolveProjectPath("best.onnx"),
                                      cfg.input_width, cfg.input_height);
    if (modelOk) {
        detector.setNmsThreshold(cfg.nms_threshold);
        std::cout << "YOLO 模型已加载" << std::endl;
    } else {
        std::cerr << "模型未加载，将只显示相机画面（无检测）" << std::endl;
    }

    // 相机
    Camera cam;
    if (!cam.open(cfg.input_width, cfg.input_height)) {
        std::cerr << "相机打开失败" << std::endl;
        return -1;
    }
    std::cout << "相机已打开 (" << cfg.input_width << "x" << cfg.input_height << ")" << std::endl;

    // 串口（可降级），通过 udev 规则 /dev/gimbal->实际设备
    SerialPort serial;
    bool serialOk = serial.open("/dev/gimbal");
    if (serialOk) {
        std::cout << "串口已打开 (/dev/gimbal)" << std::endl;
    } else {
        std::cerr << "串口未打开 (/dev/gimbal)，将只显示画面（无发送）" << std::endl;
    }

    std::cout << "按 'q' 退出 | 模型:" << (modelOk ? "true" : "false")
              << " | 串口:" << (serialOk ? "true" : "false") << std::endl;

    // 主循环
    StableTracker tracker(90);
    cv::Mat frame;
    auto lastTime = std::chrono::steady_clock::now();
    double fps = 0;
    int frameCount = 0;

    while (true) {
        auto t0 = std::chrono::steady_clock::now();
        frameCount++;

        // FPS 计算
        auto dt = std::chrono::duration<double>(t0 - lastTime).count();
        lastTime = t0;
        fps = (dt > 0) ? 1.0 / dt : 0;

        // 热重载
        if (routes.fileChanged()) {
            std::cout << "路径映射变化，重载..." << std::endl;
            routes.load(routeCfgPath);
        }
        auto nowMtime = safeLastWriteTime(visionCfgPath);
        if (nowMtime != lastVisionMtime) {
            std::cout << "视觉参数变化，重载..." << std::endl;
            cfg = loadVisionConfig(visionCfgPath);
            if (modelOk) detector.setNmsThreshold(cfg.nms_threshold);
            lastVisionMtime = nowMtime;
        }

        // 采集
        static int emptyCount = 0;
        frame = cam.getFrame();
        if (frame.empty()) {
            emptyCount++;
            // 连续 50 次空帧 → 尝试重连相机
            if (emptyCount == 50) {
                std::cerr << "连续空帧，尝试重连相机..." << std::endl;
                cam.close();
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                if (cam.open(cfg.input_width, cfg.input_height)) {
                    std::cout << "相机重连成功" << std::endl;
                }
                emptyCount = 0;
            }
            continue;
        }
        emptyCount = 0;  // 拿到正常帧，重置计数

        // 推理（有模型才跑）
        std::vector<Detection> dets;
        if (modelOk) {
            dets = detector.infer(frame, cfg.confidence_threshold);

            // 每 30 帧輸出一次診斷
            if (frameCount % 30 == 0) {
                std::cout << "[帧#" << frameCount << "] "
                          << "检测数:" << dets.size()
                          << " 帧尺寸:" << frame.cols << "x" << frame.rows
                          << " 阈值:" << cfg.confidence_threshold;
                for (size_t i = 0; i < dets.size(); ++i) {
                    const auto& d = dets[i];
                    std::cout << "\n    → " << CLASS_NAMES[d.class_id]
                              << " conf=" << d.confidence
                              << " bbox=(" << d.bbox.x << "," << d.bbox.y
                              << " " << d.bbox.width << "x" << d.bbox.height << ")";
                }
                if (dets.empty()) std::cout << " (无检出)";
                std::cout << std::endl;
            }
        }

        // 跟踪 → 查表 → 封包 → 串口发送
        if (auto target = tracker.update(dets, CLASS_NAMES)) {
            auto cmd = routes.lookup(*target);
            if (cmd) {
                // 构建数据包
                auto packet = path_serial_driver::makePacket(
                    cmd->first_cmd, cmd->second_cmd, cmd->turn_strength);

                // CRC16 校验
                crc16::Append_CRC16_Check_Sum(
                    reinterpret_cast<uint8_t*>(&packet), sizeof(packet));

                // 序列化为字节数组
                auto data = path_serial_driver::toVector(packet);

                if (serialOk) {
                    // 实际发送
                    serial.send(data.data(), data.size());
                } else {
                    // 模拟模式：只打印封包内容
                    auto cmdName = [](uint8_t v, uint8_t a, uint8_t b, uint8_t c) -> const char* {
                        if (v == a) return "直行";
                        if (v == b) return "左转";
                        if (v == c) return "右转";
                        return "?";
                    };
                    auto branchName = [](uint8_t v) -> const char* {
                        if (v == 1) return "左分支";
                        if (v == 2) return "中分支";
                        if (v == 3) return "右分支";
                        return "?";
                    };
                    std::cout << "[模拟] " << *target << std::endl;
                    std::cout << "──────────────────────────────" << std::endl;
                    std::cout << "  指令:  first=" << (int)cmd->first_cmd
                              << " (" << cmdName(cmd->first_cmd, 0, 1, 2) << ")"
                              << "  second=" << (int)cmd->second_cmd
                              << " (" << branchName(cmd->second_cmd) << ")" << std::endl;
                    std::cout << "  转弯强度: " << (int)cmd->turn_strength << std::endl;
                    std::cout << "  原始字节 (" << data.size() << "B): ";
                    for (size_t i = 0; i < data.size(); ++i)
                        std::cout << std::hex << (int)data[i] << " ";
                    std::cout << std::dec << std::endl;
                    std::cout << std::endl;
                }
            }
        }

        // 显示（waitKey 同时处理 GUI 事件 + 帧率控制）
        drawDebug(frame, dets, tracker, cfg, colors, modelOk, serialOk, fps);
        int delay = 1000 / 30;  // 目标 30fps → 33ms 间隔
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        int waitMs = std::max(5, delay - static_cast<int>(elapsed));
        if (cv::waitKey(waitMs) == 'q') break;
    }

    std::cout << "程序正常退出" << std::endl;
    return 0;
}
