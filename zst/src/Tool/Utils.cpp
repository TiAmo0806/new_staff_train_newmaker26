#include "/home/zst/zst/include/Tool/Utils.h"
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <yaml-cpp/yaml.h>

std::string makeRunTimestamp()
{
    std::time_t now = std::time(nullptr);    
    //当前时间
    std::tm local = *std::localtime(&now);
    //将时间转换为当前系统时区下的年、月、日、时、分。
    std::ostringstream oss;
    oss << std::put_time(&local, "%y%m%d%H%M");
    //格式化时间   年（26） 月（07） 日（12） 时(15) 分（38）
    return oss.str();
}

std::filesystem::path resolveProjectRoot(const std::string &configPath) //推断项目根目录
{
    if (!configPath.empty())
    {
        auto p = std::filesystem::absolute(configPath);
        if (p.has_parent_path()) return p.parent_path().parent_path();
    }
    auto cwd = std::filesystem::current_path();
    return cwd.filename() == "build" ? cwd.parent_path() : cwd;
}

bool loadAppConfig(const std::string &path, AppConfig &config)
{
    try
    {
        YAML::Node y = YAML::LoadFile(path);
        if (y["camera"])
        {
            auto c = y["camera"];
            if (c["exposure_us"]) config.camera.exposureUs = c["exposure_us"].as<int>();
            if (c["gain"]) config.camera.gain = c["gain"].as<int>();
            if (c["auto_exposure"]) config.camera.autoExposure = c["auto_exposure"].as<bool>();
        }
        if (y["model"])
        {
            auto m = y["model"];
            if (m["onnx_path"]) config.vision.yolo.modelPath = m["onnx_path"].as<std::string>();
            if (m["svm_path"]) config.vision.svmPath = m["svm_path"].as<std::string>();
            if (m["input_width"]) config.vision.yolo.inputWidth = m["input_width"].as<int>();
            if (m["input_height"]) config.vision.yolo.inputHeight = m["input_height"].as<int>();
            if (m["confidence"]) config.vision.yolo.confThreshold = m["confidence"].as<float>();
            if (m["nms_iou"]) config.vision.yolo.nmsThreshold = m["nms_iou"].as<float>();
            if (m["use_svm"]) config.vision.useSvm = m["use_svm"].as<bool>();
        }
        if (y["serial"])
        {
            auto s = y["serial"];
            if (s["port"]) config.serial.port = s["port"].as<std::string>();
            if (s["baudrate"]) config.serial.baudrate = s["baudrate"].as<int>();
            if (s["simulated"]) config.serial.simulated = s["simulated"].as<bool>();
            if (s["tx_log"]) config.serial.txLog = s["tx_log"].as<bool>();
        }
        if (y["runtime"])
        {
            auto r = y["runtime"];
            if (r["show_window"]) config.showWindow = r["show_window"].as<bool>();
            if (r["save_video"]) config.saveVideo = r["save_video"].as<bool>();
            if (r["log_dir"]) config.logDir = r["log_dir"].as<std::string>();
            if (r["terminal_line_limit"]) config.terminalLineLimit = r["terminal_line_limit"].as<int>();
        }
        if (y["workflow"])
        {
            auto w = y["workflow"];
            if (w["team_mode"])
            {
                const std::string mode = w["team_mode"].as<std::string>();
                config.workflow.mode = mode == "team_b" ? TeamMode::TeamB : TeamMode::TeamA;
            }
            if (w["vote_frames_per_stage"])  //推测表示每个业务阶段收集多少帧检测结果，再进行投票决定最终结果
                config.workflow.voteFramesPerStage = w["vote_frames_per_stage"].as<int>();
            if (w["min_hits_per_stage"])     //推测表示在一阶段的多帧检测中，至少有多少帧识别到同一结果，才认为结果有效
                config.workflow.minHitsPerStage = w["min_hits_per_stage"].as<int>();
            if (w["session_id"])
                config.workflow.sessionId = static_cast<uint8_t>(w["session_id"].as<int>());
        }
        if (y["planner"])
        {
            auto p = y["planner"];
            if (p["min_stable_frames"]) config.vision.planner.minStableFrames = p["min_stable_frames"].as<int>();
        }      //推测表示一个识别结果必须连续稳定多少帧，规划器才采取动作
        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "[Config] load failed: " << e.what() << std::endl;
        return false;
    }
}
