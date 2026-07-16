#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>

/**
 * @brief 从 config.yaml 加载的全局可调参数
 *
 * 使用方式：
 *   Config cfg;
 *   if (!cfg.loadFromYAML("config.yaml")) {
 *       fprintf(stderr, "[Config] 配置加载失败，使用默认值\n");
 *   }
 *   // 将 cfg 中的参数赋值给 Detector / VirtualSerial / Camera
 */
struct Config
{
    // ---- 模型 ----
    std::string modelPath   = "./model3/best5.xml";
    std::string device      = "AUTO";

    // ---- 检测器 ----
    float confThreshold     = 0.4f;
    float nmsThreshold      = 0.5f;
    int   inputWidth        = 640;
    int   inputHeight       = 640;

    // ---- 相机 ----
    int   cameraTimeoutMs   = 1000;

    // ---- 串口 ----
    std::string serialPort      = "/dev/ttyACM0";
    bool        txLogEnabled    = true;
    bool        autoReconnect   = true;
    int         maxRetries      = 3;

    // ---- 显示 ----
    std::string windowName      = "Real-time Detection (ESC to exit)";
    float       fpsInterval     = 1.0f;

    // ============================================================
    // 从 YAML 文件加载（OpenCV FileStorage 格式）
    // ============================================================
    bool loadFromYAML(const std::string& yamlPath);

    /// 打印当前所有参数值（调试用）
    void print() const;
};

#endif // CONFIG_HPP
