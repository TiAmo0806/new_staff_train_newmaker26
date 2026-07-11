/**
 * yolo_detector.hpp —— YOLO 检测器封装
 * 封装 ONNX Runtime 推理全流程：加载模型 → 预处理 → 推理 → 解析输出
 * 它的 Ort::Session是模板类，必须头文件能看到完整定义，所以只能这样。
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
     * @param modelPath         模型文件路径
     * @param configInputWidth  配置文件中的输入宽度（模型动态尺寸时的回退值）
     * @param configInputHeight 配置文件中的输入高度（模型动态尺寸时的回退值）
     * @param useGPU            是否使用 GPU（默认 CPU）
     * @return 成功返回 true
     */
    bool loadModel(const std::string& modelPath,
                   int configInputWidth = 640, int configInputHeight = 640,
                   bool /*useGPU*/ = false)
    ///*useGPU*/ 这个写法，是因为目前函数体内还没用到这个参数，用注释把名字"藏"起来，可以防止编译器报警告说"参数未使用"
    {
        try {
            allocator_ = Ort::AllocatorWithDefaultOptions();//初始化，后面的代码要申请临时内存，必须通过它
            //env_：运行环境，判断成员变量 env_ 是否为空。如果是空的，才去创建一个 ONNX 运行环境。
            if (!env_) {
                env_ = Ort::Env(ORT_LOGGING_LEVEL_WARNING, "YOLODetector");
            }
            Ort::SessionOptions opts;//这个类只管"存设置"，它本身不会去执行推理，它只是一个"配置容器"
            opts.SetIntraOpNumThreads(2);//既利用多核加速，又不会因为线程开太多导致 CPU 切换开销过大
            opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            //拿到这张图后启用所有能开的优化，别老老实实照着跑，推理速度明显变快
            session_ = Ort::Session(env_, modelPath.c_str(), opts);

            // 获取输入形状，用于预处理
            auto inputTypeInfo = session_.GetInputTypeInfo(0);//从会话中获取第 0 个输入的类型信息。
            auto inputTensorInfo = inputTypeInfo.GetTensorTypeAndShapeInfo();//从类型信息中提取张量的形状信息。
            auto rawShape = inputTensorInfo.GetShape();//从形状信息中提取出具体的维度向量

            if (rawShape.size() != 4) {
                std::cerr << "错误：输入形状不是4维，实际维度: " << rawShape.size() << std::endl;
                return false;
            }
            if (rawShape[2] == -1 || rawShape[3] == -1) {
                std::cerr << "警告：检测到动态输入尺寸(-1)，使用配置文件中的输入尺寸 "
                          << configInputWidth << "x" << configInputHeight << std::endl;
                inputHeight_ = configInputHeight;
                inputWidth_  = configInputWidth;
            } else {
                inputHeight_ = static_cast<int>(rawShape[2]);
                inputWidth_  = static_cast<int>(rawShape[3]);
            }

            // 获取输出信息，这个函数会把输出张量里的 numClasses（类别数）和 numAnchors（8400 个点）提取出来，打包成结构体
            outputInfo_ = getOutputInfo(session_);

            // 分配内存名称（必须与模型一致）ONNX 推理时，喂数据和取结果不靠下标，靠名字
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
        /*
        YOLO 要求输入是正方形（比如 640x640），但你的摄像头拍出来是长方形。
        预处理会把图片等比例缩放，然后在两边补灰边（Letterbox）凑成正方形。
        这三个变量就是记录"缩放了多少"、"补了多少边"，后面解析坐标时要把这些"补偿"减掉，
        才能还原真实世界的位置。
        */
        cv::Mat blob = preprocess(frame, dw, dh, scale, inputWidth_, inputHeight_);

        //构建输入 tensor（引用 blob 内存，零拷贝)
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memoryInfo_, blob.ptr<float>(), inputSize_,
            inputShape_.data(), inputShape_.size());
        /*
        Ort::Value::CreateTensor<float>：ONNX Runtime 提供的工厂方法，用来造一个张量容器。
        memoryInfo_：之前预分配的 CPU 内存描述信息。
        blob.ptr<float>()：直接把 blob 图像数据的底层内存指针传给 ONNX。
        inputSize_：总像素个数。
        inputShape_.data() 和 inputShape_.size()：输入形状 {1,3,H,W}。
        */
        //推理 
        const char* inputNames[]  = {inputName_.c_str()};
        const char* outputNames[] = {outputName_.c_str()};
        auto outputs = session_.Run(Ort::RunOptions{nullptr},
                                     inputNames, &inputTensor, 1,
                                     outputNames, 1);
        /*
        session_.Run(...)：这是 ONNX Runtime 的核心执行函数。
        Ort::RunOptions{nullptr}：运行选项，传入 nullptr 表示"全用默认配置"。
        参数依次是：输入名字列表、输入张量列表、输入个数（1个）、输出名字列表、输出个数（1个）。
        */
        
        const float* data = outputs[0].GetTensorData<float>();

        //解析输出
        if (outputInfo_.isFeatureFirst) {
            return parseYOLOv8OutputFeatureFirst(data,
                outputInfo_.numClasses, outputInfo_.numAnchors,
                frame.cols, frame.rows,
                scale, dw, dh,
                confidenceThreshold, nmsThreshold_,
                usedClasses_);
        } else {
            return parseYOLOv8OutputAnchorFirst(data,
                outputInfo_.numClasses, outputInfo_.numAnchors,
                frame.cols, frame.rows,
                scale, dw, dh,
                confidenceThreshold, nmsThreshold_,
                usedClasses_);
        }
        //通过后处理（parseYOLOv8Output）把 8400 个的候选框，精挑细选成几个精准的检测框
    }

    //设置 NMS 阈值
    void setNmsThreshold(float t) { nmsThreshold_ = t; }

    //设置只用前N类（-1=全部）
    void setUsedClasses(int n) { usedClasses_ = n; }

    //获取模型输入尺寸 
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
    int usedClasses_ = -1;
};

#endif  // YOLO_DETECTOR_HPP_
