#pragma once

#include "core/AppConfig.h"
#include "core/DetectionTypes.h"

#include <onnxruntime_cxx_api.h>
#include <opencv2/core.hpp>

#include <memory>
#include <string>
#include <vector>

class BeanNumberDetector {
public:
    /**
     * @brief 构造豆子和数字检测器。
     * @param config 检测器配置，包含后端类型、模型路径、阈值和类别映射。
     */
    explicit BeanNumberDetector(const DetectorConfig& config);

    /**
     * @brief 加载 ONNXRuntime 检测模型。
     * @return 初始化成功返回 true，失败返回 false。
     *
     * 命令驱动流程不允许回退到 mock 检测。
     */
    bool loadModel();

    /**
     * @brief 对输入图像执行检测。
     * @param frame 输入图像。
     * @return 检测框列表，每个元素是一个 Detection。
     *
     * Detector 只负责检测，不判断 P1/P2/P3，也不生成搬运任务。
     */
    std::vector<Detection> detect(const cv::Mat& frame);

private:
    /**
     * @brief 归一化类别名称。
     * @param name 原始类别名，例如 data_1 或 digit_1。
     * @return 归一化后的类别名，例如 digit_1。
     */
    std::string normalizeClassName(const std::string& name) const;

    /**
     * @brief 使用 ONNXRuntime 执行 YOLO 推理。
     * @param frame 输入图像。
     * @return 检测框列表。
     */
    std::vector<Detection> detectOnnxRuntime(const cv::Mat& frame);
    void printModelIoInfo();
    void printThreadStrategy() const;
    void recordPerformance(double preprocess_ms,
                           double inference_ms,
                           double postprocess_ms,
                           double total_ms);
    static std::string shapeToString(const std::vector<int64_t>& shape);

    DetectorConfig config_;
    Ort::Env env_;
    Ort::SessionOptions session_options_;
    std::unique_ptr<Ort::Session> session_;
    std::vector<std::string> input_names_storage_;
    std::vector<std::string> output_names_storage_;
    bool model_loaded_ = false;
    bool preprocess_layout_checked_ = false;
    size_t detect_count_ = 0;
    size_t perf_sample_count_ = 0;
    double preprocess_ms_sum_ = 0.0;
    double inference_ms_sum_ = 0.0;
    double postprocess_ms_sum_ = 0.0;
    double total_ms_sum_ = 0.0;
    std::vector<double> total_ms_samples_;
};
