/**
 * yolo_detector.hpp —— YOLO 检测器封装
 * 封装 ONNX Runtime 推理全流程：加载模型 → 预处理 → 推理 → 解析输出
 */

#ifndef YOLO_DETECTOR_HPP_
#define YOLO_DETECTOR_HPP_

#include <opencv2/opencv.hpp>
#include <onnxruntime-sdk/include/onnxruntime_cxx_api.h>

#include "detection.hpp"

class YOLODetector {
public:
    YOLODetector() = default;
    ~YOLODetector() = default;

    /**
     * @brief 加载 ONNX 模型
     * @param modelPath 模型文件路径
     * @param useGPU 是否使用 GPU（默认 CPU）
     * @return 成功返回 true
     */
    bool loadModel(const std::string& modelPath, bool /*useGPU*/ = false)
    {
        try {
            env_ = Ort::Env(ORT_LOGGING_LEVEL_WARNING, "YOLODetector");
            Ort::SessionOptions opts;
            opts.SetIntraOpNumThreads(2);
            opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

            session_ = Ort::Session(env_, modelPath.c_str(), opts);

            // 获取输入形状，用于预处理
            auto inputTypeInfo = session_.GetInputTypeInfo(0);
            auto inputTensorInfo = inputTypeInfo.GetTensorTypeAndShapeInfo();
            auto rawShape = inputTensorInfo.GetShape();
            inputWidth_  = static_cast<int>(rawShape[3]);   // NCHW
            inputHeight_ = static_cast<int>(rawShape[2]);

            // 获取输出信息
            outputInfo_ = getOutputInfo(session_);

            // 分配内存名称（必须与模型一致）
            auto inputName = session_.GetInputNameAllocated(0, allocator_);
            inputName_ = std::string(inputName.get());
            auto outputName = session_.GetOutputNameAllocated(0, allocator_);
            outputName_ = std::string(outputName.get());

            // 预分配 tensor 元数据（避免每帧重复创建）
            inputShape_ = {1, 3, inputHeight_, inputWidth_};
            inputSize_  = 1 * 3 * inputHeight_ * inputWidth_;
            memoryInfo_ = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

            std::cout << "模型加载成功: " << modelPath << std::endl;
            std::cout << "   输入尺寸: " << inputWidth_ << "x" << inputHeight_ << std::endl;
            return true;

        } catch (const Ort::Exception& e) {
            std::cerr << "ONNX 模型加载失败: " << e.what() << std::endl;
            return false;
        }
    }

    /**
     * @brief 推理一帧图像
     * @param frame 输入图像 (BGR)
     * @param confidenceThreshold 置信度阈值
     * @return 检测结果列表
     */
    std::vector<Detection> infer(const cv::Mat& frame, float confidenceThreshold)
    {
        if (frame.empty()) return {};

        // ---- 预处理 ----
        int dw, dh;
        float scale;
        cv::Mat blob = preprocess(frame, dw, dh, scale, inputWidth_, inputHeight_);

        // ---- 构建输入 tensor（引用 blob 内存，零拷贝）----
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memoryInfo_, blob.ptr<float>(), inputSize_,
            inputShape_.data(), inputShape_.size());

        // ---- 推理 ----
        const char* inputNames[]  = {inputName_.c_str()};
        const char* outputNames[] = {outputName_.c_str()};
        auto outputs = session_.Run(Ort::RunOptions{nullptr},
                                     inputNames, &inputTensor, 1,
                                     outputNames, 1);

        const float* data = outputs[0].GetTensorData<float>();

        // ---- 解析输出 ----
        return parseYOLOv8Output(data,
            outputInfo_.numClasses, outputInfo_.numAnchors,
            frame.cols, frame.rows,
            scale, dw, dh,
            confidenceThreshold, nmsThreshold_);
    }

    /** 设置 NMS 阈值 */
    void setNmsThreshold(float t) { nmsThreshold_ = t; }

    /** 获取模型输入尺寸 */
    int inputWidth()  const { return inputWidth_; }
    int inputHeight() const { return inputHeight_; }

private:
    Ort::Env env_{nullptr};
    Ort::Session session_{nullptr};
    Ort::AllocatorWithDefaultOptions allocator_;
    Ort::MemoryInfo memoryInfo_{nullptr};
    std::string inputName_;
    std::string outputName_;
    OutputInfo outputInfo_;
    std::vector<int64_t> inputShape_;
    size_t inputSize_ = 0;
    int inputWidth_ = 640;
    int inputHeight_ = 640;
    float nmsThreshold_ = 0.25f;
};

#endif  // YOLO_DETECTOR_HPP_
