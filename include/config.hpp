/*
 config.hpp —— 全局配置参数与类别名称，生存=成
 本文件集中管理所有可调参数和固定类别名称。
 可调参数从 YAML 文件加载，类别名称硬编码。
 */

#ifndef CONFIG_HPP_
#define CONFIG_HPP_

#include <opencv2/opencv.hpp>
#include <string>

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

    //相机重连参数
    int reconnect_threshold = 50;         // 连续空帧阈值
    int reconnect_delay_ms = 500;         // 重连前等待时间(ms)

    //串口重连参数
    int serial_reconnect_cooldown_ms = 5000;   // 重连冷却时间(ms)，避免频繁重连
    int serial_max_reconnect_attempts = 10;    // 最大连续重连次数，超限进入降级模式
};

//加载参数函数声明
VisionConfig loadVisionConfig(const std::string& filepath = "config/vision_config.yaml");

#endif  // CONFIG_HPP_