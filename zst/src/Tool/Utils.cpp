#include "Tool/Utils.h"
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <yaml-cpp/yaml.h>

std::string makeRunTimestamp()
{
    std::time_t now = std::time(nullptr);       // 获取当前 Unix 时间戳
    std::tm local = *std::localtime(&now);      // 转换为本地时间结构体
    std::ostringstream oss;
    oss << std::put_time(&local, "%y%m%d%H%M"); // 格式化为 YYMMDDHHMM
    return oss.str();
}

std::filesystem::path findDefaultConfigPath(const char *argv0)
{
    namespace fs = std::filesystem;
    std::error_code ec;                         // 用于捕获文件系统操作错误

    // NUC Linux 首选方法：
    // /proc/self/exe 始终指向当前正在运行的可执行文件，
    // 不受 VS Code cwd 或终端当前目录影响。
    const fs::path executablePath = fs::read_symlink("/proc/self/exe", ec);  // 读取自身可执行文件路径

    if (!ec && !executablePath.empty())
    {
        // executablePath: /任意路径/zst/build/logistics_vision
        // candidate:      /任意路径/zst/config/vision.yaml
        const fs::path candidate =
            (executablePath.parent_path() / ".." / "config" / "vision.yaml")
                .lexically_normal();            // 规范化路径，去掉中间的 ..

        if (fs::is_regular_file(candidate, ec))
            return candidate;                   // 找到配置文件，直接返回
    }

    // 如果 /proc/self/exe 不可用，尝试通过 argv[0] 定位。
    ec.clear();                                 // 清除之前的错误码
    if (argv0 != nullptr && argv0[0] != '\0')
    {
        fs::path argvPath(argv0);
        if (argvPath.is_relative())
            argvPath = fs::absolute(argvPath, ec);  // 相对路径转绝对路径

        if (!ec)
        {
            const fs::path candidate =
                (argvPath.parent_path() / ".." / "config" / "vision.yaml")
                    .lexically_normal();

            if (fs::is_regular_file(candidate, ec))
                return candidate;
        }
    }

    // 兼容从项目根目录启动：
    //   cd /任意路径/zst && ./build/logistics_vision
    ec.clear();
    const fs::path cwd = fs::current_path(ec);  // 获取当前工作目录
    if (!ec)
    {
        const fs::path rootCandidate = cwd / "config" / "vision.yaml";
        if (fs::is_regular_file(rootCandidate, ec))
            return rootCandidate;               // 从项目根目录启动的场景

        // 兼容从 build 目录启动：
        //   cd /任意路径/zst/build && ./logistics_vision
        ec.clear();
        const fs::path buildCandidate =
            (cwd / ".." / "config" / "vision.yaml").lexically_normal();
        if (fs::is_regular_file(buildCandidate, ec))
            return buildCandidate;              // 从 build 目录启动的场景
    }

    // 全部失败时返回最合理的预期位置，main 会打印并退出。
    if (!executablePath.empty())
        return (executablePath.parent_path() / ".." / "config" / "vision.yaml")
            .lexically_normal();

    return fs::path("config") / "vision.yaml";  // 最后的 fallback
}

std::filesystem::path resolveProjectRoot(const std::string &configPath)
{
    if (!configPath.empty())
    {
        auto p = std::filesystem::absolute(configPath);    // 转为绝对路径
        // config 在项目根目录的 config/ 下，父目录的父目录就是项目根目录
        if (p.has_parent_path()) return p.parent_path().parent_path();
    }
    auto cwd = std::filesystem::current_path();            // 获取当前工作目录
    return cwd.filename() == "build" ? cwd.parent_path() : cwd;  // build 目录往上退一级
}

bool loadAppConfig(const std::string &path, AppConfig &config)
{
    try
    {
        YAML::Node y = YAML::LoadFile(path);    // 加载 YAML 配置文件
        if (y["camera"])                        // 相机参数段
        {
            auto c = y["camera"];
            if (c["exposure_us"]) config.camera.exposureUs = c["exposure_us"].as<int>();
            if (c["gain"]) config.camera.gain = c["gain"].as<int>();
            if (c["auto_exposure"]) config.camera.autoExposure = c["auto_exposure"].as<bool>();
            if (c["frame_timeout_ms"])
                config.camera.frameTimeoutMs =
                    std::clamp(c["frame_timeout_ms"].as<int>(), 20, 2000);
            if (c["reconnect_after_failures"])
                config.camera.reconnectAfterFailures =
                    std::clamp(c["reconnect_after_failures"].as<int>(), 1, 100);
        }
        if (y["model"])                         // 模型参数段（YOLO + SVM）
        {
            auto m = y["model"];
            if (m["onnx_path"]) config.vision.yolo.modelPath = m["onnx_path"].as<std::string>();
            if (m["svm_path"]) config.vision.svmPath = m["svm_path"].as<std::string>();
            if (m["input_width"]) config.vision.yolo.inputWidth = m["input_width"].as<int>();
            if (m["input_height"]) config.vision.yolo.inputHeight = m["input_height"].as<int>();
            if (m["intra_op_threads"])
                config.vision.yolo.intraOpThreads =
                    std::clamp(m["intra_op_threads"].as<int>(), 0, 64);
            if (m["confidence"]) config.vision.yolo.confThreshold = m["confidence"].as<float>();
            if (m["nms_iou"]) config.vision.yolo.nmsThreshold = m["nms_iou"].as<float>();
            if (m["use_svm"]) config.vision.useSvm = m["use_svm"].as<bool>();
        }
        if (y["serial"])                        // 串口参数段
        {
            auto s = y["serial"];
            if (s["port"]) config.serial.port = s["port"].as<std::string>();
            if (s["baudrate"]) config.serial.baudrate = s["baudrate"].as<int>();
            if (s["simulated"]) config.serial.simulated = s["simulated"].as<bool>();
            if (s["tx_log"]) config.serial.txLog = s["tx_log"].as<bool>();
        }
        if (y["runtime"])                       // 运行时参数段
        {
            auto r = y["runtime"];
            if (r["show_window"]) config.showWindow = r["show_window"].as<bool>();
            if (r["save_video"]) config.saveVideo = r["save_video"].as<bool>();
            if (r["log_dir"]) config.logDir = r["log_dir"].as<std::string>();
            if (r["terminal_line_limit"]) config.terminalLineLimit = r["terminal_line_limit"].as<int>();
        }
        if (y["workflow"])                      // 比赛流程参数段
        {
            auto w = y["workflow"];
            if (w["team_mode"])
            {
                const std::string mode = w["team_mode"].as<std::string>();
                config.workflow.mode = mode == "team_b" ? TeamMode::TeamB : TeamMode::TeamA;  // 默认 team_a
            }
            if (w["vote_frames_per_stage"])
                config.workflow.voteFramesPerStage = w["vote_frames_per_stage"].as<int>();
            if (w["min_hits_per_stage"])
                config.workflow.minHitsPerStage = w["min_hits_per_stage"].as<int>();
            if (w["team_b_center_width_ratio"])
                config.workflow.teamBCenterWidthRatio =
                    w["team_b_center_width_ratio"].as<float>();
        }
        if (y["planner"])                       // 规划器参数段
        {
            auto p = y["planner"];
            if (p["min_stable_frames"]) config.vision.planner.minStableFrames = p["min_stable_frames"].as<int>();
        }

        // YAML 中的模型路径统一以项目根目录为基准，而不是以当前用户名或启动目录为基准。
        // 例如 config/vision.yaml 写 best.onnx，会解析成 <zst根目录>/best.onnx。
        const std::filesystem::path projectRoot = resolveProjectRoot(path);  // 反推项目根目录
        auto resolveModelPath = [&](std::string &value) {
            if (value.empty()) return;                                      // 空路径不处理
            std::filesystem::path modelPath(value);
            if (modelPath.is_relative())                                    // 相对路径才需要拼接
                value = (projectRoot / modelPath).lexically_normal().string();
        };
        resolveModelPath(config.vision.yolo.modelPath);     // 解析 YOLO 模型路径
        resolveModelPath(config.vision.svmPath);            // 解析 SVM 模型路径
        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "[Config] load failed: " << e.what() << std::endl;
        return false;
    }
}
