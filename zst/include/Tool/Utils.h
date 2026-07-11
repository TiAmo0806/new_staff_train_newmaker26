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
std::filesystem::path resolveProjectRoot(const std::string &configPath);
bool loadAppConfig(const std::string &path, AppConfig &config);

#endif // UTILS_H
