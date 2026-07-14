/**
 config.cpp —— 从 YAML 文件加载可调参数
 * 依赖：yaml-cpp (sudo apt install libyaml-cpp-dev)
 */

#include "../include/config.hpp"
#include <yaml-cpp/yaml.h>
#include <iostream>

VisionConfig loadVisionConfig(const std::string& filepath)
{
    VisionConfig cfg;
    
    try {
        YAML::Node root = YAML::LoadFile(filepath);
        //yaml-cpp 语法：如果配置文件无该字段 / 字段解析失败，直接使用括号内默认值
        if (root["model"])
        {
            if (root["model"]["model_path"])
                cfg.model_path = root["model"]["model_path"].as<std::string>();
        }
        if (root["detection"])
        {
            if (root["detection"]["confidence_threshold"])
                cfg.confidence_threshold = root["detection"]["confidence_threshold"].as<float>();
            if (root["detection"]["nms_threshold"])
                cfg.nms_threshold = root["detection"]["nms_threshold"].as<float>();
            if (root["detection"]["input_width"])
                cfg.input_width = root["detection"]["input_width"].as<int>();
            if (root["detection"]["input_height"])
                cfg.input_height = root["detection"]["input_height"].as<int>();
            if (root["detection"]["used_classes"])
                cfg.used_classes = root["detection"]["used_classes"].as<int>();
        }
        if (root["camera"])
        {
            if (root["camera"]["exposure_time"])
                cfg.exposure_time = root["camera"]["exposure_time"].as<double>();
            if (root["camera"]["analog_gain"])
                cfg.analog_gain = root["camera"]["analog_gain"].as<int>();
            if (root["camera"]["reconnect_threshold"])
                cfg.reconnect_threshold = root["camera"]["reconnect_threshold"].as<int>();
            if (root["camera"]["reconnect_delay_ms"])
                cfg.reconnect_delay_ms = root["camera"]["reconnect_delay_ms"].as<int>();
        }
        if (root["serial"])
        {
            if (root["serial"]["reconnect_cooldown_ms"])
                cfg.serial_reconnect_cooldown_ms = root["serial"]["reconnect_cooldown_ms"].as<int>();
            if (root["serial"]["max_reconnect_attempts"])
                cfg.serial_max_reconnect_attempts = root["serial"]["max_reconnect_attempts"].as<int>();
        }
        if (root["display"])
        {
            if (root["display"]["font_scale"])
                cfg.font_scale = root["display"]["font_scale"].as<double>();
            if (root["display"]["font_thickness"])
                cfg.font_thickness = root["display"]["font_thickness"].as<int>();
            if (root["display"]["line_thickness"])
                cfg.line_thickness = root["display"]["line_thickness"].as<int>();
        }
        
        std::cout << "从 " << filepath << " 加载视觉参数成功" << std::endl;
    } catch (const YAML::Exception& e) {
        std::cerr << "YAML解析失败: " << e.what() << std::endl;
        std::cerr << "使用默认参数" << std::endl;
    }
    
    return cfg;
}

