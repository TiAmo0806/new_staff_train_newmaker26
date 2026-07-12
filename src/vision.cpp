/**
 * vision.cpp —— NUC 视觉主程序（函数式重构）
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
#include <tuple>
#include <optional>
#include <opencv2/opencv.hpp>

#include "config.hpp"
#include "cemare.hpp"
#include "visualizer.hpp"
#include "yolo_detector.hpp"
#include "route_config.hpp"
#include "stable_tracker.hpp"
#include "packet.hpp"
#include "serial.hpp"

namespace fs = std::filesystem;

//  类型别名
using Configs = std::tuple<VisionConfig, RouteConfig, fs::file_time_type>;

struct ReloadResult {
    VisionConfig cfg;
    fs::file_time_type mtime;
    bool changed;
};

//  1. 初始化 —— 各自独立，返回值表达结果

static Configs loadAllConfigs(const std::string& visionPath,
                              const std::string& routePath)
{
    auto cfg    = loadVisionConfig(visionPath);
    auto routes = RouteConfig{};
    routes.load(routePath);
    auto lastMtime = safeLastWriteTime(visionPath);
    return {cfg, routes, lastMtime};
}

static bool
initDetector(YOLODetector& detector,
             const std::string& modelPath, const VisionConfig& cfg)
{
    bool ok = detector.loadModel(modelPath, cfg.input_width, cfg.input_height);
    if (ok) {
        detector.setNmsThreshold(cfg.nms_threshold);
        detector.setUsedClasses(cfg.used_classes);
        std::cout << "YOLO 模型已加载" << std::endl;
    } else {
        std::cerr << "模型未加载，将只显示相机画面（无检测）" << std::endl;
    }
    return ok;
}


static bool initSerial(SerialPort& serial, const std::string& port)
{
    bool ok = serial.open(port);
    if (ok)
        std::cout << "串口已打开 (" << port << ")" << std::endl;
    else
        std::cerr << "串口未打开 (" << port << ")，将只显示画面（无发送）" << std::endl;
    return ok;
}

// ============================================================
//  2. 热重载 —— 输入旧状态，输出新状态
// ============================================================

static ReloadResult
reloadVisionIfChanged(const std::string& path,
                      const VisionConfig& oldCfg,
                      const fs::file_time_type& lastMtime)
{
    auto nowMtime = safeLastWriteTime(path);
    if (nowMtime != lastMtime) {
        std::cout << "视觉参数变化，重载..." << std::endl;
        return {loadVisionConfig(path), nowMtime, true};
    }
    return {oldCfg, lastMtime, false};
}

static RouteConfig
reloadRoutesIfChanged(const std::string& path, RouteConfig routes)
{
    if (routes.fileChanged()) {
        std::cout << "路径映射变化，重载..." << std::endl;
        routes.load(path);
    }
//两套实现方式不一样——vision 用外部追踪 mtime，route 用内部fileChanged()
    return routes;
}

// ============================================================
//  3. 推理 + 诊断
// ============================================================

static std::vector<Detection>
runInference(const cv::Mat& frame, YOLODetector& detector,
             const VisionConfig& cfg, bool modelOk)
{
    if (!modelOk) return {};
    return detector.infer(frame, cfg.confidence_threshold);
}

static void printDiagnostics(int frameCount,
                             const std::vector<Detection>& dets,
                             const cv::Mat& frame,
                             const VisionConfig& cfg)
{
    if (frameCount % 30 != 0) return;

    std::cout << "[帧#" << frameCount << "] "
              << "检测数:" << dets.size()
              << " 帧尺寸:" << frame.cols << "x" << frame.rows
              << " 阈值:" << cfg.confidence_threshold;

    for (const auto& d : dets) {
        std::cout << "\n    → " << CLASS_NAMES[d.class_id]
                  << " conf=" << d.confidence
                  << " bbox=(" << d.bbox.x << "," << d.bbox.y
                  << " " << d.bbox.width << "x" << d.bbox.height << ")";
    }
    if (dets.empty()) std::cout << " (无检出)";
    std::cout << std::endl;
}

// ============================================================
//  5. 指令发送 —— 封包 → CRC → 发送（或模拟打印）
// ============================================================

static void sendCommand(const std::string& targetName,
                        uint8_t firstCmd, uint8_t secondCmd, uint8_t turnStrength,
                        SerialPort& serial, bool serialOk)
{
    auto packet = path_serial_driver::makePacket(firstCmd, secondCmd, turnStrength);
    auto data   = path_serial_driver::pack(packet);   // CRC16 + 序列化

    if (serialOk) {
        serial.sendPacket(data);   // 重试 + 重连 + 日志 由 serial 内部处理
        return;
    }

    // 模拟模式

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
    std::cout << "[模拟] " << targetName << std::endl;
    std::cout << "──────────────────────────────" << std::endl;
    std::cout << "  指令:  first=" << (int)firstCmd
              << " (" << cmdName(firstCmd, 0, 1, 2) << ")"
              << "  second=" << (int)secondCmd
              << " (" << branchName(secondCmd) << ")" << std::endl;
    std::cout << "  转弯强度: " << (int)turnStrength << std::endl;
    std::cout << "  原始字节 (" << data.size() << "B): ";
    for (size_t i = 0; i < data.size(); ++i)
        std::cout << std::hex << (int)data[i] << " ";
    std::cout << std::dec << "\n" << std::endl;
}

// ============================================================
//  6. 显示
// ============================================================

static bool renderFrame(cv::Mat& frame,
                        const std::vector<Detection>& dets,
                        const StableTracker& tracker,
                        const VisionConfig& cfg,
                        const std::vector<cv::Scalar>& colors,
                        bool modelOk, bool serialOk, double fps,
                        const std::chrono::steady_clock::time_point& t0)
{
    drawDebug(frame, dets, tracker, cfg, colors, modelOk, serialOk, fps);

    constexpr int targetFps = 30;
    int delay   = 1000 / targetFps;
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - t0)
                       .count();
    int waitMs = std::max(5, delay - static_cast<int>(elapsed));
    return cv::waitKey(waitMs) == 'q';
}

// ============================================================
//  7. 主循环 —— 编排 pipeline
// ============================================================

static void runLoop(Camera& cam,
                    YOLODetector& detector,
                    SerialPort& serial,
                    VisionConfig         cfg,
                    RouteConfig          routes,
                    const std::string&   visionPath,
                    const std::string&   routePath,
                    bool                 modelOk,
                    bool                 serialOk,
                    const std::vector<cv::Scalar>& colors)
{
    auto lastVisionMtime = safeLastWriteTime(visionPath);
    StableTracker tracker(90);
    int frameCount   = 0;
    double fps       = 0.0;
    auto lastTime    = std::chrono::steady_clock::now();

    std::cout << "按 'q' 退出 | 模型:" << (modelOk ? "true" : "false")
              << " | 串口:" << (serialOk ? "true" : "false") << std::endl;

    while (true) {
        auto t0 = std::chrono::steady_clock::now();
        frameCount++;

        // FPS
        {
            auto dt = std::chrono::duration<double>(t0 - lastTime).count();
            lastTime = t0;
            fps = (dt > 0) ? 1.0 / dt : 0.0;
        }

        // 热重载
        routes = reloadRoutesIfChanged(routePath, std::move(routes));
        {
            auto [newCfg, newMtime, changed] =
                reloadVisionIfChanged(visionPath, cfg, lastVisionMtime);
            if (changed) {
                cfg = newCfg;
                lastVisionMtime = newMtime;
                if (modelOk) {
                    detector.setNmsThreshold(cfg.nms_threshold);
                    detector.setUsedClasses(cfg.used_classes);
                }
            }
        }

        // 采集（Camera 内部状态机处理重连，不阻塞主线程）
        auto maybeFrame = cam.getFrameSafe(cfg.reconnect_threshold,
                                           cfg.reconnect_delay_ms);
        if (!maybeFrame) continue;   // 空帧或重连等待中，下一轮
        auto& frame = *maybeFrame;

        // 推理
        auto dets = runInference(frame, detector, cfg, modelOk);
        printDiagnostics(frameCount, dets, frame, cfg);

        // 跟踪 → 查表 → 发送
        if (auto target = tracker.update(dets)) {
            if (auto cmd = routes.lookup(*target)) {
                sendCommand(*target, cmd->first_cmd, cmd->second_cmd,
                            cmd->turn_strength, serial, serialOk);
            }
        }

        // 显示
        if (renderFrame(frame, dets, tracker, cfg, colors,
                        modelOk, serialOk, fps, t0))
            break;
    }

    std::cout << "程序正常退出" << std::endl;
}

// ============================================================
//  8. 入口 —— 纯组装
// ============================================================

int main()
{
    std::cout << "NUC 视觉节点启动" << std::endl;

    const auto visionPath = resolveProjectPath("vision_config.yaml");
    const auto routePath  = resolveProjectPath("route_config.txt");

    // 初始化 —— 每个组件独立
    auto [cfg, routes, lastMtime] = loadAllConfigs(visionPath, routePath);
    auto colors                   = buildColorTable(CLASS_NAMES.size());

    YOLODetector detector;
    bool modelOk = initDetector(detector, resolveProjectPath("best.onnx"), cfg);

    Camera cam;
    if (!cam.open(cfg.input_width, cfg.input_height)) {
        std::cerr << "相机打开失败" << std::endl;
        return -1;
    }
    std::cout << "相机已打开 (" << cfg.input_width << "x"
              << cfg.input_height << ")" << std::endl;

    SerialPort serial;
    bool serialOk = initSerial(serial, "/dev/gimbal");

    // 进入主循环（所有权移交）
    runLoop(cam, detector, serial,
            cfg, routes,
            visionPath, routePath,
            modelOk, serialOk,
            colors);
    return 0;
}
