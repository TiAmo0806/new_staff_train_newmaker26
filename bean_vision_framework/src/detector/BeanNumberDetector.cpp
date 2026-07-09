#include "detector/BeanNumberDetector.h"

#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>

namespace {

struct LetterboxInfo {
    float scale = 1.0f;     // 原图缩放到模型输入尺寸时使用的比例。
    int pad_x = 0;          // letterbox 左右方向补边像素数。
    int pad_y = 0;          // letterbox 上下方向补边像素数。
};

/**
 * @brief 将数值限制在指定范围内。
 * @param value 待限制的数值。
 * @param low 下界。
 * @param high 上界。
 * @return 限制后的数值。
 */
float clampValue(float value, float low, float high) {
    return std::max(low, std::min(value, high));
}

/**
 * @brief 按 YOLO 训练方式缩放并补边图像。
 * @param image 原始 BGR 图像。
 * @param input_size 模型输入边长。
 * @param info 输出缩放比例和补边信息，用于后处理还原坐标。
 * @return 适配模型输入尺寸后的 BGR 图像。
 */
cv::Mat letterbox(const cv::Mat& image, int input_size, LetterboxInfo& info) {
    const int width = image.cols;
    const int height = image.rows;
    info.scale = std::min(static_cast<float>(input_size) / width, static_cast<float>(input_size) / height);

    const int new_width = static_cast<int>(std::round(width * info.scale));
    const int new_height = static_cast<int>(std::round(height * info.scale));
    info.pad_x = (input_size - new_width) / 2;
    info.pad_y = (input_size - new_height) / 2;

    cv::Mat resized;
    cv::resize(image, resized, cv::Size(new_width, new_height));

    cv::Mat output(input_size, input_size, CV_8UC3, cv::Scalar(114, 114, 114));
    resized.copyTo(output(cv::Rect(info.pad_x, info.pad_y, new_width, new_height)));
    return output;
}

/**
 * @brief 将 BGR 图像转换成 ONNXRuntime 输入张量数据。
 * @param bgr_image 已经 letterbox 到模型输入尺寸的 BGR 图像。
 * @return RGB、float32、归一化后的 CHW 排列数据。
 */
std::vector<float> makeInputTensor(const cv::Mat& bgr_image) {
    cv::Mat rgb;
    cv::cvtColor(bgr_image, rgb, cv::COLOR_BGR2RGB);
    rgb.convertTo(rgb, CV_32F, 1.0 / 255.0);

    const int channels = 3;
    const int height = rgb.rows;
    const int width = rgb.cols;
    std::vector<float> input(channels * height * width);

    for (int c = 0; c < channels; ++c) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                input[c * height * width + y * width + x] = rgb.at<cv::Vec3f>(y, x)[c];
            }
        }
    }
    return input;
}

}  // namespace

/**
 * @brief 构造豆子和数字检测器。
 * @param config 检测器配置。
 */
BeanNumberDetector::BeanNumberDetector(const DetectorConfig& config)
    : config_(config), env_(ORT_LOGGING_LEVEL_WARNING, "bean_vision_framework") {}

/**
 * @brief 加载 YOLO/ONNXRuntime 模型。
 * @return 加载成功返回 true。
 */
bool BeanNumberDetector::loadModel() {
    model_loaded_ = false;

    if (config_.backend == "mock") {
        std::cerr << "Mock detector is disabled for command-driven flow. Use backend=onnxruntime.\n";
        return false;
    }

    if (config_.backend != "onnxruntime") {
        std::cerr << "Backend '" << config_.backend
                  << "' is not implemented. Use backend=onnxruntime.\n";
        return false;
    }

    if (config_.model_path.empty() || !std::filesystem::exists(config_.model_path)) {
        std::cerr << "YOLO model not found: " << config_.model_path << "\n";
        return false;
    }

    try {
        // 按已验证示例保持单线程推理，并启用 ORT 全量图优化。
        session_options_.SetIntraOpNumThreads(1);
        session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        session_ = std::make_unique<Ort::Session>(env_, config_.model_path.c_str(), session_options_);

        Ort::AllocatorWithDefaultOptions allocator;
        input_names_storage_.clear();
        output_names_storage_.clear();
        // ONNXRuntime 返回的名称指针由 AllocatedStringPtr 管理，这里复制成 std::string 保存。
        for (size_t i = 0; i < session_->GetInputCount(); ++i) {
            Ort::AllocatedStringPtr name = session_->GetInputNameAllocated(i, allocator);
            input_names_storage_.emplace_back(name.get());
        }
        for (size_t i = 0; i < session_->GetOutputCount(); ++i) {
            Ort::AllocatedStringPtr name = session_->GetOutputNameAllocated(i, allocator);
            output_names_storage_.emplace_back(name.get());
        }

        model_loaded_ = session_ != nullptr && !input_names_storage_.empty() && !output_names_storage_.empty();
    } catch (const Ort::Exception& e) {
        std::cerr << "Failed to load YOLO model with ONNXRuntime: " << e.what() << "\n";
        model_loaded_ = false;
    }

    if (model_loaded_) {
        std::cout << "Detector backend: onnxruntime model=" << config_.model_path << "\n";
    }
    return model_loaded_;
}

/**
 * @brief 对单帧图像执行检测。
 * @param frame 输入图像。
 * @return 检测框列表。
 */
std::vector<Detection> BeanNumberDetector::detect(const cv::Mat& frame) {
    if (config_.backend == "onnxruntime") {
        return detectOnnxRuntime(frame);
    }
    std::cerr << "[ERROR] detector backend is unavailable: " << config_.backend << "\n";
    return {};
}

/**
 * @brief 根据别名表归一化模型类别名。
 * @param name 原始类别名。
 * @return 归一化后的类别名。
 */
std::string BeanNumberDetector::normalizeClassName(const std::string& name) const {
    const auto it = config_.aliases.find(name);
    if (it != config_.aliases.end()) {
        return it->second;
    }
    return name;
}

/**
 * @brief 使用 ONNXRuntime 执行 YOLO 推理并转换为 Detection。
 * @param frame 输入图像。
 * @return 检测框列表。
 */
std::vector<Detection> BeanNumberDetector::detectOnnxRuntime(const cv::Mat& frame) {
    std::vector<Detection> detections;
    if (!model_loaded_ || session_ == nullptr || frame.empty()) {
        return detections;
    }

    constexpr int input_size = 640;
    LetterboxInfo info;
    // 先 letterbox，后处理时再用同一组缩放和补边参数还原到原图坐标。
    cv::Mat input_image = letterbox(frame, input_size, info);
    std::vector<float> input_tensor_values = makeInputTensor(input_image);

    Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::array<int64_t, 4> input_shape = {1, 3, input_size, input_size};
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info,
        input_tensor_values.data(),
        input_tensor_values.size(),
        input_shape.data(),
        input_shape.size());

    std::vector<const char*> input_names;
    std::vector<const char*> output_names;
    for (const std::string& name : input_names_storage_) {
        input_names.push_back(name.c_str());
    }
    for (const std::string& name : output_names_storage_) {
        output_names.push_back(name.c_str());
    }

    std::vector<Ort::Value> outputs;
    try {
        outputs = session_->Run(
            Ort::RunOptions{nullptr},
            input_names.data(),
            &input_tensor,
            1,
            output_names.data(),
            output_names.size());
    } catch (const Ort::Exception& e) {
        std::cerr << "[ERROR] ONNXRuntime inference failed: " << e.what() << "\n";
        return detections;
    }

    if (outputs.empty() || !outputs[0].IsTensor()) {
        return detections;
    }

    float* output_data = outputs[0].GetTensorMutableData<float>();
    const std::vector<int64_t> output_shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
    if (output_shape.size() != 3) {
        std::cerr << "[ERROR] unsupported YOLO output rank: " << output_shape.size() << "\n";
        return detections;
    }

    const int rows = static_cast<int>(output_shape[1]);
    const int cols = static_cast<int>(output_shape[2]);
    if (rows <= 4 || cols <= 0) {
        return detections;
    }

    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> class_ids;

    // 当前 YOLO 导出模型输出格式为 1 x C x N：前 4 行是框，后续行是类别分数。
    for (int i = 0; i < cols; ++i) {
        float best_score = 0.0f;
        int best_class = -1;
        for (int c = 4; c < rows; ++c) {
            const float score = output_data[c * cols + i];
            if (score > best_score) {
                best_score = score;
                best_class = c - 4;
            }
        }

        if (best_score < config_.conf_threshold || best_class < 0) {
            continue;
        }

        const float cx = output_data[0 * cols + i];
        const float cy = output_data[1 * cols + i];
        const float width = output_data[2 * cols + i];
        const float height = output_data[3 * cols + i];

        // 模型坐标位于 letterbox 图上，需要先去掉补边，再按缩放比例还原回原图。
        float x1 = (cx - width * 0.5f - info.pad_x) / info.scale;
        float y1 = (cy - height * 0.5f - info.pad_y) / info.scale;
        float x2 = (cx + width * 0.5f - info.pad_x) / info.scale;
        float y2 = (cy + height * 0.5f - info.pad_y) / info.scale;

        x1 = clampValue(x1, 0.0f, static_cast<float>(frame.cols - 1));
        y1 = clampValue(y1, 0.0f, static_cast<float>(frame.rows - 1));
        x2 = clampValue(x2, 0.0f, static_cast<float>(frame.cols - 1));
        y2 = clampValue(y2, 0.0f, static_cast<float>(frame.rows - 1));

        const int box_x = static_cast<int>(std::round(x1));
        const int box_y = static_cast<int>(std::round(y1));
        const int box_w = static_cast<int>(std::round(x2 - x1));
        const int box_h = static_cast<int>(std::round(y2 - y1));
        if (box_w <= 0 || box_h <= 0) {
            continue;
        }

        boxes.emplace_back(box_x, box_y, box_w, box_h);
        confidences.push_back(best_score);
        class_ids.push_back(best_class);
    }

    std::vector<int> keep;
    cv::dnn::NMSBoxes(boxes, confidences, config_.conf_threshold, config_.nms_threshold, keep);

    detections.reserve(keep.size());
    for (int index : keep) {
        const int class_id = class_ids[index];
        const auto name_it = config_.names.find(class_id);
        Detection detection;
        detection.class_id = class_id;
        detection.class_name = normalizeClassName(
            name_it != config_.names.end() ? name_it->second : std::to_string(class_id));
        detection.confidence = confidences[index];
        detection.box = boxes[index];
        detections.push_back(detection);
    }

    std::sort(detections.begin(), detections.end(), [](const Detection& a, const Detection& b) {
        return a.confidence > b.confidence;
    });
    return detections;
}
