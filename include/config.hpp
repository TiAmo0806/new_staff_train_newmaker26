/*
 config.hpp —— 全局配置参数与类别名称，生存=成
 本文件集中管理所有可调参数和固定类别名称。
 可调参数从 YAML 文件加载，类别名称硬编码。
 */

#ifndef CONFIG_HPP_
#define CONFIG_HPP_

#include <opencv2/opencv.hpp>
#include <string>
#include <filesystem>
#include <iostream>

#include "utils.hpp"

namespace fs = std::filesystem;

//类别名称固定不变
//顺序 YOLO 模型输出完全一致
inline const std::vector<std::string> CLASS_NAMES = {
    "soybean",              
    "mung_bean",            
    "white_kidney_bean",    
    "1",                    
    "2",                    
    "3",                    
    "4",                    
    "5"                     
};

//可调参数结构体
struct VisionConfig
{
    //模型参数
    std::string model_path = "best.onnx";    // 模型文件路径
    std::string fallback_model_path = "";    // 备用模型路径（主模型失败时自动切换）

    //模型推理参数
    float confidence_threshold = 0.35f;   // 置信度阈值
    float nms_threshold = 0.25f;          // NMS IoU 阈值
    int input_width = 640;                // 模型输入宽度
    int input_height = 640;               // 模型输入高度
    int used_classes = -1;                // 只用前N类（-1=全部）

    //显示参数
    double font_scale = 0.6;              // 文字大小
    int font_thickness = 2;               // 文字粗细
    int line_thickness = 2;               // 检测框边框粗细

    //相机曝光参数
    double exposure_time = -1.0;           // 手动曝光时间(微秒)，-1=自动曝光
    int analog_gain = -1;                  // 模拟增益，-1=自动

    //相机重连参数
    int reconnect_threshold = 50;         // 连续空帧阈值
    int reconnect_delay_ms = 500;         // 重连前等待时间(ms)
    int target_fps = 30;                  // 主循环帧率上限

    //串口重连参数
    bool serial_strict_mode = false;            // 串口打开失败时直接退出（true=退出，false=降级）
    int serial_reconnect_cooldown_ms = 5000;   // 重连冷却时间(ms)，避免频繁重连
    int serial_max_reconnect_attempts = 10;    // 最大连续重连次数，超限进入降级模式

    //稳定跟踪参数
    int stable_threshold = 90;               // 连续确认帧数
};

//  热重载结果
struct ReloadResult {
    VisionConfig cfg;
    fs::file_time_type mtime;
    bool changed;
};

//加载参数函数声明
VisionConfig loadVisionConfig(const std::string& filepath = "config/vision_config.yaml");

//  热重载：检测到 YAML 文件变化则重新加载
inline ReloadResult reloadVisionIfChanged(const std::string& path,
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

#endif  // CONFIG_HPP_