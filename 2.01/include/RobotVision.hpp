/**
 * @file RobotVision.hpp
 * @brief YOLOv8 ONNX 推理引擎 — 豆子与数字标签识别
 *
 * 使用 ONNX Runtime 加载 YOLOv8 ONNX 模型进行实时目标检测。
 * 模型输出格式: [1, 12, 8400] (NCHW)
 *   - 通道 0-3: 边界框 cx, cy, w, h（在 640×640 输入坐标系中）
 *   - 通道 4-11: 8 类别得分
 *
 * 识别的 8 个类别（class_id 0~7）:
 *   0 ~ 2:  豆子类别       — soybean(黄豆), mung_bean(绿豆), white_kidney_bean(白芸豆)
 *   3 ~ 5:  目标数字标签    — data_1 ~ data_3（放置区的 1~3 号箱）
 *   6 ~ 7:  忽略数字标签    — data_4 ~ data_5（放置区的 4~5 号箱，本任务不使用）
 *
 * 比赛背景：
 *   取豆区有 3 个箱子，每个箱子装一种豆子（排列随机）。
 *   放置区有 5 个箱子，分别贴着数字 1~5（排列随机）。
 *   视觉需要识别豆子种类和数字标签，配合状态机建立位置映射。
 */

#ifndef ROBOT_VISION_HPP
#define ROBOT_VISION_HPP

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <map>
#include <mutex>
#include <memory>
#include <onnxruntime_cxx_api.h>

class RobotVision {
public:
    /**
     * @brief 单次检测结果
     *
     * 包含类别、置信度、边界框（原始图像坐标系）和中心点。
     * 所有坐标已经过 letterbox 反算，可以直接在原始 frame 上绘制。
     */
    struct Detection {
        int class_id;           // 类别 ID (0~7)
        std::string class_name; // 类别名称（如 "soybean", "data_1"）
        float confidence;       // 置信度 [0, 1]
        cv::Rect bbox;          // 边界框（原始图像坐标系）
        cv::Point2f center;     // 中心点（原始图像坐标系）
    };

    /**
     * @brief 一帧图像的完整推理结果
     *
     * 按类别将检测框分流：
     *   beans         — class_id 0~2 的豆子检测
     *   digits        — class_id 3~7 的所有数字标签
     *   target_digits — class_id 3~5（本任务关心的 data_1~3）
     *   ignore_digits — class_id 6~7（本任务忽略的 data_4~5）
     */
    struct ClassificationResult {
        std::vector<Detection> beans;
        std::vector<Detection> digits;
        std::vector<Detection> target_digits;
        std::vector<Detection> ignore_digits;
    };

    // 内置类别名称表（供外部查询用）
    static const std::map<int, std::string>& getClassNames();

private:
    // PIMPL 模式：隐藏 ONNX Runtime 的实现细节
    class Impl;
    std::unique_ptr<Impl> pImpl;

    // ⭐ 类别判断辅助函数（class_id 含义见文件注释）
    inline bool isBean(int class_id) { return class_id >= 0 && class_id <= 2; }
    inline bool isDigit(int class_id) { return class_id >= 3 && class_id <= 7; }
    inline bool isTargetDigit(int class_id) { return class_id >= 3 && class_id <= 5; }
    inline bool isIgnoreDigit(int class_id) { return class_id >= 6 && class_id <= 7; }

public:
    RobotVision();
    explicit RobotVision(const std::string& model_path, float conf_thresh = 0.3);
    ~RobotVision();

    void loadModel(const std::string& model_path, float conf_thresh = 0.3);

    /**
     * @brief 对一帧图像执行 YOLOv8 推理
     * @param frame 输入图像（BGR 格式，任意尺寸）
     * @return 按类别分组的检测结果（坐标已映射回原始图像）
     *
     * 处理流程：letterbox 预处理 → BGR→RGB → 归一化 → ONNX Runtime 推理
     *         → 解析输出 → 坐标反算 → NMS → 分拣 → 后处理过滤
     */
    ClassificationResult infer(const cv::Mat& frame);

    // 从 class_id 提取数字箱号: class_id=3 → box_number=1, class_id=4 → 2, ...
    int getDigitValue(int class_id) { return class_id - 2; }
    std::string getBeanName(int class_id);
    void setConfidenceThreshold(float thresh);

private:
    /**
     * @brief letterbox 缩放信息
     *
     * 记录将原始图像缩放并居中填充到 640×640 时使用的参数，
     * 推理后将坐标从模型输入空间反算回原始图像空间。
     */
    struct LetterBoxInfo {
        float scale = 1.0f;     // 缩放系数（原始→目标）
        int pad_left = 0;       // 左侧填充像素数
        int pad_top = 0;        // 顶部填充像素数
    };

    /**
     * @brief 图像预处理
     *
     * 1. 保持宽高比的 letterbox 缩放（不足部分填充灰色 114）
     * 2. BGR → RGB 通道转换（模型训练时使用 RGB）
     * 3. 归一化到 [0, 1]
     *
     * @param frame  原始 BGR 图像
     * @param lb_info [out] 填充信息，用于后续坐标反算
     * @return 预处理后的 float32 图像（640×640）
     */
    cv::Mat preprocess(const cv::Mat& frame, LetterBoxInfo& lb_info);

    /**
     * @brief 按类别分组执行 NMS（非极大值抑制）
     *
     * 避免不同类别的目标互相抑制（同一位置可能有豆子和数字标签共存）。
     * 先按类别分组，再在每个组内执行 OpenCV 的 NMSBoxes。
     */
    void applyNMS(std::vector<Detection>& detections);
};

#endif