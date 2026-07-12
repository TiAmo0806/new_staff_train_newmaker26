#ifndef UTILS_H
#define UTILS_H

#include "CameraDriver/MindVisionCamera.h"
#include "Communication/VirtualSerial.h"
#include "ImgProcessing/VisionSystem.h"
#include "ImgProcessing/CompetitionWorkflow.h"
#include <filesystem>
#include <string>

struct AppConfig
{
    CameraConfig camera;                    // 工业相机参数：曝光、增益等
    VisionSystemConfig vision;              // YOLO/SVM/规划器参数
    SerialConfig serial;                    // 串口参数：端口名、波特率、模拟模式
    CompetitionWorkflowConfig workflow;     // 比赛流程参数：队伍模式、投票帧数
    bool showWindow = true;                 // 是否显示 OpenCV 调试窗口
    bool saveVideo = false;                 // 是否保存调试视频
    std::string logDir = "logs";            // 日志输出目录
    int terminalLineLimit = 80;             // 终端输出行数限制
};

// 生成运行时间戳，格式：YYMMDDHHMM，用于日志文件名
std::string makeRunTimestamp();
// 根据配置文件路径反推项目根目录（往上两级）
std::filesystem::path resolveProjectRoot(const std::string &configPath);

// 查找程序默认配置文件，不依赖启动时的当前工作目录（CWD）。
// Linux 优先根据 /proc/self/exe 找到可执行文件所在目录，
// 再定位 ../config/vision.yaml。
// 如果该方法不可用，则依次尝试 argv[0] 和当前工作目录。
std::filesystem::path findDefaultConfigPath(const char *argv0);

bool loadAppConfig(const std::string &path, AppConfig &config);

#endif // UTILS_H
