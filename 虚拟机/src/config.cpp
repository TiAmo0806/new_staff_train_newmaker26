#include "config.hpp"

#include <opencv2/core/persistence.hpp>  // cv::FileStorage
#include <stdio.h>

// 辅助宏：从 FileStorage node 安全读取，保持默认值
#define CFG_READ(node, key, field) \
    if (!node[#key].empty()) { node[#key] >> field; }

bool Config::loadFromYAML(const std::string& yamlPath)
{
    cv::FileStorage fs(yamlPath, cv::FileStorage::READ);
    if (!fs.isOpened()) {
        fprintf(stderr, "[Config] ERROR: 无法打开配置文件 %s\n", yamlPath.c_str());
        return false;
    }

    printf("[Config] 加载配置文件: %s\n", yamlPath.c_str());

    // ---- 模型 ----
    if (!fs["model"].empty()) {
        cv::FileNode model = fs["model"];
        CFG_READ(model, path,   modelPath);
        CFG_READ(model, device, device);
    }

    // ---- 检测器 ----
    if (!fs["detector"].empty()) {
        cv::FileNode det = fs["detector"];
        CFG_READ(det, conf_threshold, confThreshold);
        CFG_READ(det, nms_threshold,  nmsThreshold);
        CFG_READ(det, input_width,    inputWidth);
        CFG_READ(det, input_height,   inputHeight);
    }

    // ---- 相机 ----
    if (!fs["camera"].empty()) {
        cv::FileNode cam = fs["camera"];
        CFG_READ(cam, timeout_ms, cameraTimeoutMs);
    }

    // ---- 串口 ----
    if (!fs["serial"].empty()) {
        cv::FileNode ser = fs["serial"];
        CFG_READ(ser, port,            serialPort);
        CFG_READ(ser, tx_log_enabled,  txLogEnabled);
        CFG_READ(ser, auto_reconnect,  autoReconnect);
        CFG_READ(ser, max_retries,     maxRetries);
    }

    // ---- 显示 ----
    if (!fs["display"].empty()) {
        cv::FileNode dsp = fs["display"];
        CFG_READ(dsp, window_name,          windowName);
        CFG_READ(dsp, fps_sample_interval,  fpsInterval);
    }

    fs.release();
    print();
    return true;
}

void Config::print() const
{
    printf("\n"
           "=============== 当前配置 ===============\n"
           " [模型]\n"
           "   modelPath:       %s\n"
           "   device:          %s\n"
           " [检测器]\n"
           "   confThreshold:   %.2f\n"
           "   nmsThreshold:    %.2f\n"
           "   inputSize:       %d x %d\n"
           " [相机]\n"
           "   cameraTimeoutMs: %d\n"
           " [串口]\n"
           "   serialPort:      %s\n"
           "   txLogEnabled:    %s\n"
           "   autoReconnect:   %s\n"
           "   maxRetries:      %d\n"
           " [显示]\n"
           "   windowName:      %s\n"
           "   fpsInterval:     %.1f\n"
           "=========================================\n\n",
           modelPath.c_str(),
           device.c_str(),
           confThreshold,
           nmsThreshold,
           inputWidth, inputHeight,
           cameraTimeoutMs,
           serialPort.c_str(),
           txLogEnabled ? "true" : "false",
           autoReconnect  ? "true" : "false",
           maxRetries,
           windowName.c_str(),
           fpsInterval);
}
