#ifndef UTILS_H
#define UTILS_H

#include "CameraDriver/MindVisionCamera.h"
#include "Communication/VirtualSerial.h"
#include "ImgProcessing/CompetitionWorkflow.h"
#include "ImgProcessing/VisionSystem.h"
#include <filesystem>
#include <string>

// 程序运行方式：比赛模式由电控开关相机；调试模式启动后直接打开相机。
enum class AppRunMode
{
    Competition,
    Debug
};

// 应用总配置：把YAML各段解析为对应模块能够直接使用的强类型配置。
struct AppConfig
{
    CameraConfig camera;                    // 工业相机参数：曝光、增益、超时和重连
    VisionSystemConfig vision;              // OpenVINO YOLO和调试规划器参数
    SerialConfig serial;                    // 串口设备、波特率、模拟模式和TX日志
    CompetitionWorkflowConfig workflow;     // A/B流程、投票和断点续跑参数
    AppRunMode runMode = AppRunMode::Competition; // 默认比赛模式，避免上电后意外启动相机
    bool showWindow = false;                // 是否显示OpenCV调试窗口，与competition/debug流程模式独立
    bool terminalDetectionLog = true;       // 是否周期性打印当前帧按X排序后的识别结果
    int terminalDetectionIntervalFrames = 10; // 每隔多少帧打印一次，避免终端刷屏
    std::string logDir = "logs";            // 预留：当前主循环尚未写日志文件
    int terminalLineLimit = 80;             // 预留：当前主循环尚未限制终端行数
};

// 生成运行时间戳，格式YYMMDDHHMM；保留给后续日志/录像文件命名。
std::string makeRunTimestamp();

// 根据config/vision.yaml的位置向上两级得到zst项目根目录。
std::filesystem::path resolveProjectRoot(const std::string &configPath);

// 默认配置查找不依赖当前工作目录：Linux优先使用/proc/self/exe定位可执行文件。
std::filesystem::path findDefaultConfigPath(const char *argv0);

// 读取YAML、限制危险参数范围，并把模型和断点相对路径转换为项目绝对路径。
bool loadAppConfig(const std::string &path, AppConfig &config);

#endif // UTILS_H
