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
        cfg.confidence_threshold = root["confidence_threshold"].as<float>(0.5f);
        cfg.nms_threshold = root["nms_threshold"].as<float>(0.25f);
        cfg.input_width = root["input_width"].as<int>(640);
        cfg.input_height = root["input_height"].as<int>(640);
        cfg.font_scale = root["font_scale"].as<double>(0.6);
        cfg.font_thickness = root["font_thickness"].as<int>(2);
        cfg.line_thickness = root["line_thickness"].as<int>(2);
        
        std::cout << "从 " << filepath << " 加载视觉参数成功" << std::endl;
    } catch (const YAML::Exception& e) {
        std::cerr << "YAML解析失败: " << e.what() << std::endl;
        std::cerr << "使用默认参数" << std::endl;
    }
    
    return cfg;
}

//  生成颜色表（每个类别一种颜色，通过 HSV 色相均匀分布）
std::vector<cv::Scalar> buildColorTable(int numClasses)
{
    std::vector<cv::Scalar> colors;
    for (int i = 0; i < numClasses; ++i) {
        // 在 HSV 空间中均匀分布色相
        int hue = static_cast<int>(180.0 * i / numClasses);
        cv::Mat hsv(1, 1, CV_8UC3, cv::Scalar(hue, 255, 255));
        cv::Mat bgr;
        cv::cvtColor(hsv, bgr, cv::COLOR_HSV2BGR);
        colors.push_back(cv::Scalar(bgr.at<cv::Vec3b>(0)));
    }
    return colors;
}