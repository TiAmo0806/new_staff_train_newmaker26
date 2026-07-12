#ifndef UTILS_H
#define UTILS_H

#include "/home/zst/zst/include/CameraDriver/MindVisionCamera.h"
#include "/home/zst/zst/include/Communication/VirtualSerial.h"
#include "/home/zst/zst/include/ImgProcessing/VisionSystem.h"
#include "/home/zst/zst/include/ImgProcessing/CompetitionWorkflow.h"
#include <filesystem>
#include <string>

struct AppConfig
{
    CameraConfig camera;
    VisionSystemConfig vision;
    SerialConfig serial;
    CompetitionWorkflowConfig workflow;
    bool showWindow = true;
    bool saveVideo = false;
    std::string logDir = "logs";
    int terminalLineLimit = 80;
};

std::string makeRunTimestamp();     
 //时间戳函数，生成当前时间字符串
std::filesystem::path resolveProjectRoot(const std::string &configPath);  
 //根据配置文件位置或当前目录推断项目根目录
bool loadAppConfig(const std::string &path, AppConfig &config);            
 //从 YAML 文件填充 AppConfig

#endif // UTILS_H
