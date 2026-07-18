#include "Tool/Utils.h"
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <yaml-cpp/yaml.h>

std::string makeRunTimestamp()
{
    std::time_t now = std::time(nullptr);       // 获取当前Unix时间戳
    std::tm local = *std::localtime(&now);      // 转换为本地时区时间
    std::ostringstream oss;
    oss << std::put_time(&local, "%y%m%d%H%M"); // 格式化为YYMMDDHHMM
    return oss.str();
}

std::filesystem::path findDefaultConfigPath(const char *argv0)
{
    namespace fs = std::filesystem;
    std::error_code ec;

    // NUC Linux首选：/proc/self/exe指向真正运行的可执行文件，不受终端CWD影响。
    const fs::path executablePath = fs::read_symlink("/proc/self/exe", ec);
    if (!ec && !executablePath.empty())
    {
        const fs::path candidate =
            (executablePath.parent_path() / ".." / "config" / "vision.yaml")
                .lexically_normal();
        if (fs::is_regular_file(candidate, ec)) return candidate;
    }

    // /proc不可用时，尝试从argv[0]反推build目录和项目根目录。
    ec.clear();
    if (argv0 != nullptr && argv0[0] != '\0')
    {
        fs::path argvPath(argv0);
        if (argvPath.is_relative()) argvPath = fs::absolute(argvPath, ec);
        if (!ec)
        {
            const fs::path candidate =
                (argvPath.parent_path() / ".." / "config" / "vision.yaml")
                    .lexically_normal();
            if (fs::is_regular_file(candidate, ec)) return candidate;
        }
    }

    // 再兼容从zst根目录或build目录手动启动的情况。
    ec.clear();
    const fs::path cwd = fs::current_path(ec);
    if (!ec)
    {
        const fs::path rootCandidate = cwd / "config" / "vision.yaml";
        if (fs::is_regular_file(rootCandidate, ec)) return rootCandidate;

        ec.clear();
        const fs::path buildCandidate =
            (cwd / ".." / "config" / "vision.yaml").lexically_normal();
        if (fs::is_regular_file(buildCandidate, ec)) return buildCandidate;
    }

    // 全部未找到时返回最合理位置，main会打印该路径并报告加载失败。
    if (!executablePath.empty())
        return (executablePath.parent_path() / ".." / "config" / "vision.yaml")
            .lexically_normal();
    return fs::path("config") / "vision.yaml";
}

std::filesystem::path resolveProjectRoot(const std::string &configPath)
{
    if (!configPath.empty())
    {
        const std::filesystem::path p = std::filesystem::absolute(configPath);
        if (p.has_parent_path()) return p.parent_path().parent_path();
    }

    const std::filesystem::path cwd = std::filesystem::current_path();
    return cwd.filename() == "build" ? cwd.parent_path() : cwd;
}

bool loadAppConfig(const std::string &path, AppConfig &config)
{
    try
    {
        const YAML::Node y = YAML::LoadFile(path);

        if (y["camera"])
        {
            const auto c = y["camera"];
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

        if (y["model"])
        {
            const auto m = y["model"];
            if (m["onnx_path"]) config.vision.yolo.modelPath = m["onnx_path"].as<std::string>();
            if (m["device"]) config.vision.yolo.device = m["device"].as<std::string>();
            if (m["cache_dir"]) config.vision.yolo.cacheDir = m["cache_dir"].as<std::string>();
            if (m["input_width"]) config.vision.yolo.inputWidth = m["input_width"].as<int>();
            if (m["input_height"]) config.vision.yolo.inputHeight = m["input_height"].as<int>();
            if (m["intra_op_threads"])
                config.vision.yolo.intraOpThreads =
                    std::clamp(m["intra_op_threads"].as<int>(), 0, 64);
            if (m["confidence"]) config.vision.yolo.confThreshold = m["confidence"].as<float>();
            if (m["nms_iou"]) config.vision.yolo.nmsThreshold = m["nms_iou"].as<float>();
        }

        if (y["serial"])
        {
            const auto s = y["serial"];
            if (s["port"]) config.serial.port = s["port"].as<std::string>();
            if (s["baudrate"]) config.serial.baudrate = s["baudrate"].as<int>();
            if (s["simulated"]) config.serial.simulated = s["simulated"].as<bool>();
            if (s["tx_log"]) config.serial.txLog = s["tx_log"].as<bool>();
            if (s["rx_log"]) config.serial.rxLog = s["rx_log"].as<bool>();
        }

        if (y["runtime"])
        {
            const auto r = y["runtime"];
            if (r["mode"])
            {
                // competition：等待电控camera_state；debug：启动后直接打开相机。
                const std::string mode = r["mode"].as<std::string>();
                if (mode == "competition")
                    config.runMode = AppRunMode::Competition;
                else if (mode == "debug")
                    config.runMode = AppRunMode::Debug;
                else
                {
                    // 不接受拼写错误后静默进入调试模式，防止比赛时相机意外启动。
                    std::cerr << "[Config] runtime.mode只能是competition或debug，当前值="
                              << mode << std::endl;
                    return false;
                }
            }
            if (r["show_window"]) config.showWindow = r["show_window"].as<bool>();
            if (r["log_dir"]) config.logDir = r["log_dir"].as<std::string>();
            if (r["terminal_line_limit"]) config.terminalLineLimit = r["terminal_line_limit"].as<int>();
        }

        if (y["workflow"])
        {
            const auto w = y["workflow"];
            if (w["team_mode"])
            {
                const std::string mode = w["team_mode"].as<std::string>();
                config.workflow.mode = mode == "team_b" ? TeamMode::TeamB : TeamMode::TeamA;
            }
            if (w["vote_frames_per_stage"])
                config.workflow.voteFramesPerStage = w["vote_frames_per_stage"].as<int>();
            if (w["min_hits_per_stage"])
                config.workflow.minHitsPerStage = w["min_hits_per_stage"].as<int>();
            if (w["team_b_center_width_ratio"])
                config.workflow.teamBCenterWidthRatio =
                    w["team_b_center_width_ratio"].as<float>();
            if (w["resume_progress"])
                config.workflow.resumeProgress = w["resume_progress"].as<bool>();
            if (w["clear_progress_on_start"])
                config.workflow.clearProgressOnStart =
                    w["clear_progress_on_start"].as<bool>();
            if (w["progress_file"])
                config.workflow.progressFile = w["progress_file"].as<std::string>();
        }

        if (y["planner"])
        {
            const auto p = y["planner"];
            if (p["min_stable_frames"])
                config.vision.planner.minStableFrames = p["min_stable_frames"].as<int>();
        }

        // 所有部署文件统一以zst项目根目录为基准，而不是以用户名或启动目录为基准。
        const std::filesystem::path projectRoot = resolveProjectRoot(path);
        auto resolveProjectPath = [&](std::string &value) {
            if (value.empty()) return;
            const std::filesystem::path relativeOrAbsolute(value);
            if (relativeOrAbsolute.is_relative())
                value = (projectRoot / relativeOrAbsolute).lexically_normal().string();
        };
        resolveProjectPath(config.vision.yolo.modelPath);
        resolveProjectPath(config.vision.yolo.cacheDir);
        resolveProjectPath(config.workflow.progressFile);
        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "[Config] load failed: " << e.what() << std::endl;
        return false;
    }
}
