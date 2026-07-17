/**
 * ov_detector.hpp —— OpenVINO YOLO 检测器封装
 *
 * 替代 ONNX Runtime，使用 OpenVINO C++ API 2.0。
 */

#ifndef OV_DETECTOR_HPP_
#define OV_DETECTOR_HPP_

#include <chrono>
#include <filesystem>
#include <thread>
#include <iostream>
#include <stdexcept>
#include <opencv2/opencv.hpp>
#include <openvino/openvino.hpp>

#include "detection.hpp"

class OVDetector {
public:
    //使用编译器生成的默认构造函数，不进行任何特殊初始化。
    //OpenVINO 对象（core_、compiledModel_）通过后续的 loadModel 来赋值。
    OVDetector() = default;

    /**
     * @brief 加载 ONNX 模型（OpenVINO 可直接吃 ONNX）
     * @param modelPath         模型文件路径 (.onnx)
     * @param configInputWidth  配置文件中的输入宽度（动态模型回退值）
     * @param configInputHeight 配置文件中的输入高度（动态模型回退值）
     * @param useGPU            是否优先使用 GPU（Intel 核显）
     * @return 成功返回 true
     */
    bool loadModel(const std::string& modelPath,
                   int configInputWidth = 640, int configInputHeight = 640,
                   bool useGPU = false)
    {
        try {
            // ov::Core：OpenVINO 的顶层管理对象，负责管理设备、读取模型、编译模型。
            core_ = ov::Core();
            std::shared_ptr<ov::Model> model;

            try {
                model = core_.read_model(modelPath);
            } catch (const std::exception& e) {
                std::cerr << "读取模型失败: " << e.what() << std::endl;
                return false;
            }

            // ── 获取输入/输出信息 ──
            auto inputShape = model->input().get_shape();
            if (inputShape.size() != 4) {
                std::cerr << "错误：输入形状不是4维，实际维度: " << inputShape.size() << std::endl;
                return false;
            }

            // 处理动态维度
            if (inputShape[2] <= 0 || inputShape[3] <= 0) {
                std::cerr << "警告：检测到动态输入尺寸，使用配置文件中的输入尺寸 "
                          << configInputWidth << "x" << configInputHeight << std::endl;
                inputHeight_ = configInputHeight;
                inputWidth_  = configInputWidth;
                model->reshape({1, 3,
                                static_cast<int64_t>(inputHeight_),
                                static_cast<int64_t>(inputWidth_)});
                //如果模型导出时使用了 -1 动态尺寸，可以在加载时显式重设形状，不用重新导出模型。
            } else {
                inputHeight_ = static_cast<int>(inputShape[2]);
                inputWidth_  = static_cast<int>(inputShape[3]);
            }

            outputInfo_ = getOutputInfo(model->output(0).get_shape());

            // ── 选择设备 ──
            std::string device = selectDevice(useGPU);
            compiledModel_ = core_.compile_model(model, device);
            inferRequest_ = compiledModel_.create_infer_request();
            /*
            selectDevice：自定义函数，扫描可用设备（GPU、CPU），根据 useGPU 参数和实际硬件情况自动选择。
            compile_model：将模型编译成目标设备可执行的代码（对应 ONNX Runtime 的 Session 创建）。
            create_infer_request：创建一次推理请求的句柄，后续每次推理复用这个请求
            */

            // ── 预分配 ──
            modelInputShape_ = ov::Shape{1, 3,
                                         static_cast<size_t>(inputHeight_),
                                         static_cast<size_t>(inputWidth_)};
            modelInputSize_  = 1 * 3 * inputHeight_ * inputWidth_;

            std::cout << "模型加载成功 [" << device
                      << "]: " << modelPath << std::endl;
            std::cout << "   输入尺寸: " << inputWidth_ << "x" << inputHeight_ << std::endl;
            return true;

        } catch (const std::exception& e) {
            std::cerr << "OpenVINO 模型加载失败: " << e.what() << std::endl;
            return false;
        }
    }

    /**
     * @brief 带重试和预检的模型加载
     */
    bool loadModelWithRetry(const std::string& modelPath,
                            int maxRetries = 3,
                            int retryDelayMs = 1000,
                            int configInputWidth = 640,
                            int configInputHeight = 640)
    {
        namespace fs = std::filesystem;

        // ── 预检 ──
        if (!fs::exists(modelPath)) {
            std::cerr << "[预检失败] 模型文件不存在: " << modelPath << std::endl;
            return false;
        }
        if (fs::file_size(modelPath) < 1024) {
            std::cerr << "[预检失败] 模型文件过小(<1KB): " << modelPath << std::endl;
            return false;
        }

        // ── 重试 ──
        for (int attempt = 0; attempt < maxRetries; ++attempt) {
            if (loadModel(modelPath, configInputWidth, configInputHeight))
                return true;

            if (attempt < maxRetries - 1) {
                std::cerr << "模型加载失败，"
                          << (attempt + 1) << "/" << maxRetries
                          << " 次尝试，等待 " << retryDelayMs << "ms 后重试..."
                          << std::endl;
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(retryDelayMs));
            }
        }

        std::cerr << "模型加载失败，已重试 " << maxRetries
                  << " 次，程序将退出" << std::endl;
        return false;
    }

    /**
     * @brief 推理一帧图像
     * @param frame                输入图像 (BGR)
     * @param confidenceThreshold  置信度阈值
     * @return 检测结果列表
     */
    std::vector<Detection> infer(const cv::Mat& frame, float confidenceThreshold)
    {
        if (frame.empty()) return {};

        // ---- 预处理（复用 OpenCV 逻辑，不改动）----
        int dw, dh;
        float scale;
        cv::Mat blob = preprocess(frame, dw, dh, scale, inputWidth_, inputHeight_);

        // ---- 构建 OpenVINO 输入 tensor（零拷贝）----
        ov::Tensor inputTensor(ov::element::f32, modelInputShape_, blob.ptr<float>());
        inferRequest_.set_input_tensor(inputTensor);

        // ---- 推理 ----
        inferRequest_.infer();

        // ---- 取输出 ----
        auto outputTensor = inferRequest_.get_output_tensor();
        const float* data = outputTensor.data<float>();

        // ---- 解析输出（YOLOv8/v11 格式：[classes+4, anchors]）----
        return parseYOLOv8OutputFeatureFirst(data,
            outputInfo_.numClasses, outputInfo_.numAnchors,
            frame.cols, frame.rows,
            scale, dw, dh,
            confidenceThreshold, nmsThreshold_,
            usedClasses_);
    }

    // ── 参数设置 ──
    void setNmsThreshold(float t) { nmsThreshold_ = t; }
    void setUsedClasses(int n)  { usedClasses_  = n; }

    // ── 尺寸查询 ──
    int inputWidth()  const { return inputWidth_; }
    int inputHeight() const { return inputHeight_; }

private:
    /// 自动选择最优设备
    static std::string selectDevice(bool preferGPU)
    {
        ov::Core tmp;
        auto devices = tmp.get_available_devices();

        if (devices.empty())
            throw std::runtime_error("没有可用的 OpenVINO 设备");

        bool hasGPU = false, hasCPU = false;
        for (const auto& d : devices) {
            if (d.find("GPU") != std::string::npos) hasGPU = true;
            if (d == "CPU") hasCPU = true;
        }

        if (preferGPU && hasGPU) return "GPU";
        if (hasCPU) return "CPU";
        return devices[0];  // 兜底
    }

    // ── OpenVINO 对象 ──
    ov::Core core_;
    ov::CompiledModel compiledModel_;
    ov::InferRequest inferRequest_;

    // ── 模型元信息 ──
    OutputInfo outputInfo_;
    ov::Shape modelInputShape_;
    size_t modelInputSize_ = 0;
    int inputWidth_  = 640;
    int inputHeight_ = 640;

    // ── 可调参数 ──
    float nmsThreshold_ = 0.25f;
    int usedClasses_    = -1;
};

#endif  // OV_DETECTOR_HPP_
