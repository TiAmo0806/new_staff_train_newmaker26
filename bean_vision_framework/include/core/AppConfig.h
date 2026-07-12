#pragma once

#include <opencv2/core.hpp>

#include <map>
#include <string>

/**
 * @brief 运行模式配置。
 *
 * mode 只描述当前程序运行场景，具体差异仍由 input/command/serial/debug 等子配置控制。
 */
struct RuntimeConfig {
    std::string mode = "debug_command_image";   // debug_command_image / debug_camera_mock / debug_image_real_serial / real_robot。
};

/**
 * @brief 图像输入配置。
 *
 * 只描述图像从哪里来，不包含检测、ROI 或串口相关配置。
 */
struct InputConfig {
    std::string type = "mock";      // 输入类型：mock / image / video / camera / mindvision_camera。
    std::string source;             // 默认图片或视频路径，兼容旧配置中的 path。
    std::string bean_path;          // image 模式下豆子阶段专用图片路径，空时回退到 source。
    std::string digit_path;         // image 模式下数字阶段专用图片路径，空时回退到 source。
    int camera_id = 0;              // 摄像头编号，camera 模式下使用。
};

/**
 * @brief 工业相机配置。
 *
 * CameraManager 和 OpenCV camera 模式都可以复用这份相机参数配置。
 */
struct CameraConfig {
    int camera_id = 0;              // 相机编号。
    int width = 1280;               // 相机宽度，camera/mindvision_camera 模式下使用。
    int height = 720;               // 相机高度。
    int fps = 30;                   // 相机帧率。
    bool auto_exposure = false;     // 是否启用自动曝光。
    double exposure_time = 8000.0;  // 曝光时间，单位微秒。
    bool auto_gain = false;         // 是否启用自动增益，当前最小实现下仅保留配置语义。
    double gain = 4.0;              // 模拟增益放大倍数。
    bool auto_white_balance = false; // 是否启用自动白平衡。
    bool flip_horizontal = false;   // 输出图像是否水平翻转。
    bool flip_vertical = false;     // 输出图像是否垂直翻转。
    int rotate = 0;                 // 输出图像旋转角度：0/90/180/270。
};

/**
 * @brief 命令来源配置。
 *
 * terminal 用于虚拟机内手动输入 arrive_bean/arrive_digit；
 * serial 预留给真实 C 板命令包；none 用于纯图像循环调试。
 */
struct CommandConfig {
    std::string source = "terminal"; // terminal / serial / none。
};

/**
 * @brief 多帧稳定识别配置。
 */
struct ScanConfig {
    int frames_per_scan = 5;            // 每次扫描采集的帧数。
    int min_vote_count = 3;             // 同一类别至少出现次数。
    float min_avg_confidence = 0.6f;    // 同一类别平均置信度阈值。
    int max_retry = 3;                  // 最大重试组数。
    int stable_delay_ms = 100;          // 收到命令后等待车体稳定的时间。
};

/**
 * @brief 检测器配置。
 *
 * 负责描述模型后端、模型路径、阈值和类别映射。
 */
struct DetectorConfig {
    std::string backend = "mock";   // 检测后端：mock / onnxruntime / opencv_dnn。
    std::string model_path;         // 真实模型路径。
    float conf_threshold = 0.25f;   // 置信度阈值。
    float nms_threshold = 0.45f;    // NMS 阈值，后续真实 YOLO 后端会用到。
    std::string class_file;         // 类别配置文件路径。
    std::map<int, std::string> names;               // 类别编号到类别名称的映射。
    std::map<std::string, std::string> aliases;     // 类别别名映射，例如 data_1 -> digit_1。
};

/**
 * @brief ROI 配置。
 *
 * pickup_rois 保存 P1/P2/P3，place_rois 保存 L4-L8。
 */
struct RoiConfig {
    std::map<std::string, cv::Rect> pickup_rois;    // 取货区 ROI。
    std::map<std::string, cv::Rect> place_rois;     // 放置区 ROI。
};

/**
 * @brief 串口配置。
 *
 * 当前教学框架默认 mock=true，只打印 TX 数据，不打开真实串口。
 */
struct SerialConfig {
    bool enable = false;            // 是否启用真实串口。
    bool mock = true;               // 是否只打印 TX 数据，不实际发送。
    std::string port = "/dev/ttyACM0";  // 串口设备名。
    int baudrate = 115200;          // 串口波特率。
    int ack_timeout_ms = 100;       // 等待 ACK 的超时时间，<=0 时不等待 ACK。
    int max_resend = 3;             // ACK 超时后的最大重发次数。
    bool print_packet_hex = true;   // 是否打印协议包十六进制。
    bool print_rx_hex = false;      // 是否打印串口收到的完整协议包。
    bool print_tx_hex = true;       // 是否打印串口发送的完整协议包。
    bool print_parsed_packet = false; // 是否打印协议包的解析结果。
};

/**
 * @brief 调试开关配置。
 */
struct DebugConfig {
    bool show_window = false;       // 是否显示 OpenCV 窗口。
    bool draw_result = true;        // 是否在图像上绘制检测和解析结果。
    bool print_detections = true;   // 是否打印检测、ROI、任务结果。
    bool print_roi_result = true;   // 是否打印 ROI 解析结果。
    bool print_vote_result = true;  // 是否打印多帧投票结果。
    bool print_state = true;        // 是否打印状态机状态变化。
    bool print_packet_hex = true;   // 是否打印协议包十六进制。
    bool print_rx_hex = false;      // 是否打印接收十六进制。
    bool print_tx_hex = true;       // 是否打印发送十六进制。
    bool print_parsed_packet = false; // 是否打印协议包解析结果。
    bool save_raw_frame = false;    // 是否保存原始输入帧。
    bool save_result_image = false; // 是否保存绘制后的调试结果图。
    bool show_mouse_position = false; // ROI 预览 demo 是否显示鼠标坐标。
    std::string output_dir = "debug_output"; // 调试图片输出目录。
};

/**
 * @brief 应用总配置。
 *
 * AppConfig 是所有子配置的汇总。main.cpp 会把不同子配置分发给对应模块。
 */
struct AppConfig {
    RuntimeConfig runtime;          // 运行模式配置。
    InputConfig input;              // 图像输入配置。
    CameraConfig camera;            // 相机参数配置。
    CommandConfig command;          // 命令来源配置。
    ScanConfig scan;                // 多帧扫描配置。
    DetectorConfig detector;        // 检测器配置。
    RoiConfig roi;                  // ROI 配置。
    SerialConfig serial;            // 串口配置。
    DebugConfig debug;              // 调试配置。
    std::string base_dir;           // app.yaml 所在目录，解析相对路径时使用。

    /**
     * @brief 从配置文件加载完整应用配置。
     * @param path app.yaml 的路径。
     * @return 加载完成的 AppConfig。
     *
     * 该函数会继续加载 classes.yaml、roi.yaml 和 serial.yaml。
     */
    static AppConfig load(const std::string& path);
};
