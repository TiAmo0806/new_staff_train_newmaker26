/**
 * RobotVision.cpp —— YOLOv8 ONNX 推理 + 豆子/数字识别
 *
 * 使用 ONNX Runtime 加载 YOLOv8 ONNX 模型进行实时推理，
 * 输出解析方式参考 cameradetect / VisionNode.cpp 的成熟实现。
 * 模型输出格式: [1, 12, 8400] (NCHW)
 *   通道 0-3: cx, cy, w, h (在 640×640 坐标系中)
 *   通道 4-11: 8 类得分
 */

#include "../include/RobotVision.hpp"

#include <algorithm>
#include <iostream>
#include <numeric>

// ============================================================
//  内置类别名称表（单一来源）
// ============================================================
static const std::map<int, std::string> CLASS_NAMES_MAP = {
    {0, "soybean"},
    {1, "mung_bean"},
    {2, "white_kidney_bean"},
    {3, "data_1"},
    {4, "data_2"},
    {5, "data_3"},
    {6, "data_4"},
    {7, "data_5"}
};

const std::map<int, std::string>& RobotVision::getClassNames() {
    return CLASS_NAMES_MAP;
}

// ============================================================
//  Impl（封装的实现细节）
// ============================================================
class RobotVision::Impl {
public:
    Ort::Env                    env{ORT_LOGGING_LEVEL_WARNING, "RobotVision"};
    Ort::SessionOptions         session_options;
    Ort::Session                session{nullptr};

    std::array<int64_t, 4>      input_shape{};   // NCHW
    int                         input_width  = 640;
    int                         input_height = 640;
    float                       conf_threshold = 0.3f;
    float                       nms_threshold  = 0.2f;

    std::mutex                  model_mutex;
    bool                        model_loaded = false;

    Impl() {
        session_options.SetIntraOpNumThreads(4);
        session_options.SetGraphOptimizationLevel(
            GraphOptimizationLevel::ORT_ENABLE_ALL);
    }
};

// ============================================================
//  构造函数 / 析构函数
// ============================================================
RobotVision::RobotVision(const std::string& model_path, float conf_thresh)
    : pImpl(std::make_unique<Impl>()) {
    loadModel(model_path, conf_thresh);
}

RobotVision::RobotVision() : pImpl(std::make_unique<Impl>()) {
    std::cout << "[Vision] 模拟模式 (无模型加载)" << std::endl;
}

RobotVision::~RobotVision() = default;

// ============================================================
//  loadModel —— 加载 ONNX 模型
// ============================================================
void RobotVision::loadModel(const std::string& model_path, float conf_thresh) {
    std::lock_guard<std::mutex> lock(pImpl->model_mutex);
    pImpl->conf_threshold = conf_thresh;

    std::cout << "[Vision] Loading model: " << model_path << std::endl;

    try {
        pImpl->session = Ort::Session(pImpl->env, model_path.c_str(),
                                      pImpl->session_options);

        auto input_type_info   = pImpl->session.GetInputTypeInfo(0);
        auto input_tensor_info = input_type_info.GetTensorTypeAndShapeInfo();
        auto shape_vec         = input_tensor_info.GetShape();
        for (size_t i = 0; i < 4 && i < shape_vec.size(); ++i)
            pImpl->input_shape[i] = shape_vec[i];

        pImpl->input_height = static_cast<int>(pImpl->input_shape[2]);
        pImpl->input_width  = static_cast<int>(pImpl->input_shape[3]);

        // 打印输出形状
        auto output_type_info = pImpl->session.GetOutputTypeInfo(0);
        auto output_shape     = output_type_info.GetTensorTypeAndShapeInfo().GetShape();
        std::cout << "[Vision] Input:  NCHW = " << pImpl->input_shape[0]
                  << " " << pImpl->input_shape[1]
                  << " " << pImpl->input_shape[2]
                  << " " << pImpl->input_shape[3] << std::endl;
        std::cout << "[Vision] Output:";
        for (auto d : output_shape) std::cout << " " << d;
        std::cout << std::endl;

        pImpl->model_loaded = true;
        std::cout << "[Vision] Model loaded successfully!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[Vision] loadModel() failed: " << e.what() << std::endl;
        pImpl->model_loaded = false;
        throw;
    }
}

// ============================================================
//  预处理 —— 保持宽高比的 letterbox 缩放 + BGR→RGB + 归一化
//  (参考 cameradetect 和 VisionNode.cpp 的实现)
// ============================================================
cv::Mat RobotVision::preprocess(const cv::Mat& frame, LetterBoxInfo& lb_info) {
    int target_w = pImpl->input_width;
    int target_h = pImpl->input_height;

    // 计算缩放比例（保持宽高比）
    float scale = std::min(static_cast<float>(target_w) / frame.cols,
                           static_cast<float>(target_h) / frame.rows);
    int new_w = static_cast<int>(frame.cols * scale);
    int new_h = static_cast<int>(frame.rows * scale);
    int pad_left = (target_w - new_w) / 2;
    int pad_top  = (target_h - new_h) / 2;

    lb_info.scale    = scale;
    lb_info.pad_left = pad_left;
    lb_info.pad_top  = pad_top;

    // 缩放
    cv::Mat resized;
    cv::resize(frame, resized, cv::Size(new_w, new_h));

    // 灰色画布 + 居中放置（letterbox）
    cv::Mat canvas(target_h, target_w, CV_8UC3, cv::Scalar(114, 114, 114));
    resized.copyTo(canvas(cv::Rect(pad_left, pad_top, new_w, new_h)));

    // ⭐ BGR → RGB（模型训练时使用 RGB，必须保持一致）
    cv::Mat rgb;
    cv::cvtColor(canvas, rgb, cv::COLOR_BGR2RGB);

    // 归一化到 [0, 1] 并转为 float
    cv::Mat float_img;
    rgb.convertTo(float_img, CV_32FC3, 1.0f / 255.0f);

    return float_img;
}

// ============================================================
//  infer —— 推理入口
//  返回豆子和数字的分类检测结果
// ============================================================
RobotVision::ClassificationResult RobotVision::infer(const cv::Mat& frame) {
    ClassificationResult result;

    if (!pImpl->model_loaded) {
        std::cerr << "[Vision] Model not loaded!" << std::endl;
        return result;
    }
    if (frame.empty()) return result;

    std::lock_guard<std::mutex> lock(pImpl->model_mutex);

    try {
        // ---- 1. 预处理 ----
        LetterBoxInfo lb_info;
        cv::Mat preprocessed = preprocess(frame, lb_info);

        // ---- 2. HWC → CHW 平面数组 ----
        int target_area = pImpl->input_width * pImpl->input_height;
        std::vector<float> input_data(3 * target_area);
        float* dst = input_data.data();

        for (int c = 0; c < 3; ++c) {
            for (int i = 0; i < target_area; ++i) {
                dst[c * target_area + i] = preprocessed.at<cv::Vec3f>(i)[c];
            }
        }

        // ---- 3. ONNX Runtime 推理 ----
        Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(
            OrtArenaAllocator, OrtMemTypeDefault);

        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            mem_info, input_data.data(), input_data.size(),
            pImpl->input_shape.data(), pImpl->input_shape.size());

        Ort::AllocatorWithDefaultOptions allocator;
        auto input_name_ptr  = pImpl->session.GetInputNameAllocated(0, allocator);
        auto output_name_ptr = pImpl->session.GetOutputNameAllocated(0, allocator);
        const char* input_names[]  = { input_name_ptr.get() };
        const char* output_names[] = { output_name_ptr.get() };

        auto output_tensors = pImpl->session.Run(
            Ort::RunOptions{nullptr},
            input_names, &input_tensor, 1,
            output_names, 1);

        // ---- 4. 复制输出数据（防止后端 buffer 失效） ----
        auto output_info   = output_tensors[0].GetTensorTypeAndShapeInfo();
        auto output_shape  = output_info.GetShape();
        size_t data_len    = output_info.GetElementCount();
        std::vector<float> output_copy(data_len);
        memcpy(output_copy.data(),
               output_tensors[0].GetTensorData<float>(),
               data_len * sizeof(float));
        const float* output_data = output_copy.data();

        if (output_shape.size() < 3) {
            std::cerr << "[Vision] Bad output shape: " << output_shape.size() << "D" << std::endl;
            return result;
        }

        int total_channels  = static_cast<int>(output_shape[1]);
        int num_detections  = static_cast<int>(output_shape[2]);
        int num_classes     = total_channels - 4;

        if (num_classes < 1 || num_classes > 100) return result;

        // ---- 5. 遍历所有检测 ----

        // ---- 6. 解析 YOLO 原始输出 ----
        std::vector<Detection> all_detections;

        for (int i = 0; i < num_detections; ++i) {
            // [1, C, N] 布局: offset = c * num_detections + i
            float cx = output_data[0 * num_detections + i];
            float cy = output_data[1 * num_detections + i];
            float w  = output_data[2 * num_detections + i];
            float h  = output_data[3 * num_detections + i];

            // 找最高得分类别
            float max_conf = 0.0f;
            int best_class = -1;
            for (int c = 0; c < num_classes; ++c) {
                float conf = output_data[(4 + c) * num_detections + i];
                if (conf > max_conf) {
                    max_conf  = conf;
                    best_class = c;
                }
            }

            if (best_class < 0 || max_conf < pImpl->conf_threshold) continue;

            // 转为角点格式
            float x1 = cx - w * 0.5f;
            float y1 = cy - h * 0.5f;
            float x2 = cx + w * 0.5f;
            float y2 = cy + h * 0.5f;

            // 裁剪到模型输入边界
            const int IMG_W = pImpl->input_width;
            const int IMG_H = pImpl->input_height;
            if (x1 < 0) x1 = 0;
            if (y1 < 0) y1 = 0;
            if (x2 > IMG_W) x2 = IMG_W;
            if (y2 > IMG_H) y2 = IMG_H;
            if (x2 <= x1 || y2 <= y1) continue;

            // 坐标还原：去掉 letterbox padding，缩放到原始分辨率
            float orig_x1 = (x1 - lb_info.pad_left) / lb_info.scale;
            float orig_y1 = (y1 - lb_info.pad_top)  / lb_info.scale;
            float orig_x2 = (x2 - lb_info.pad_left) / lb_info.scale;
            float orig_y2 = (y2 - lb_info.pad_top)  / lb_info.scale;

            if (orig_x1 < 0) orig_x1 = 0;
            if (orig_y1 < 0) orig_y1 = 0;
            if (orig_x2 > frame.cols) orig_x2 = frame.cols;
            if (orig_y2 > frame.rows) orig_y2 = frame.rows;
            if (orig_x2 <= orig_x1 || orig_y2 <= orig_y1) continue;

            Detection det;
            det.class_id   = best_class;
            det.class_name = CLASS_NAMES_MAP.at(best_class);
            det.confidence = max_conf;
            det.bbox = cv::Rect(
                static_cast<int>(orig_x1),
                static_cast<int>(orig_y1),
                static_cast<int>(orig_x2 - orig_x1),
                static_cast<int>(orig_y2 - orig_y1)
            );
            det.center = cv::Point2f(
                (cx - lb_info.pad_left) / lb_info.scale,
                (cy - lb_info.pad_top)  / lb_info.scale
            );

            all_detections.push_back(det);
        }

        // ---- 7. NMS（按类别分组，避免不同类互相抑制） ----
        applyNMS(all_detections);

        // ---- 8. 按类别分类 ----
        for (const auto& det : all_detections) {
            if (isBean(det.class_id)) {
                result.beans.push_back(det);
            } else if (isDigit(det.class_id)) {
                result.digits.push_back(det);
                if (isTargetDigit(det.class_id))
                    result.target_digits.push_back(det);
                else if (isIgnoreDigit(det.class_id))
                    result.ignore_digits.push_back(det);
            }
        }

        // ---- 9. 豆子后处理过滤 ----
        double frame_area = static_cast<double>(frame.cols) * frame.rows;

        // 9a. 排除面积过大、宽高比异常的框
        {
            std::vector<Detection> clean;
            for (const auto& det : result.beans) {
                double area = static_cast<double>(det.bbox.width) * det.bbox.height;
                if (area > frame_area * 0.55) continue;
                float aspect = static_cast<float>(det.bbox.width) / det.bbox.height;
                if (aspect > 4.0f || aspect < 0.25f) continue;
                clean.push_back(det);
            }
            result.beans = clean;
        }

        // 9b. 跨类重叠抑制
        {
            std::vector<Detection> deduped;
            for (size_t i = 0; i < result.beans.size(); ++i) {
                bool suppressed = false;
                for (size_t j = 0; j < result.beans.size(); ++j) {
                    if (i == j) continue;
                    if (result.beans[i].class_id == result.beans[j].class_id) continue;
                    cv::Rect inter = result.beans[i].bbox & result.beans[j].bbox;
                    if (inter.area() == 0) continue;
                    double iou = inter.area() / static_cast<double>(
                        result.beans[i].bbox.area() + result.beans[j].bbox.area() - inter.area());
                    if (iou > 0.3 && result.beans[i].confidence < result.beans[j].confidence) {
                        suppressed = true;
                        break;
                    }
                }
                if (!suppressed) deduped.push_back(result.beans[i]);
            }
            result.beans = deduped;
        }

    } catch (const std::exception& e) {
        std::cerr << "[Vision] Inference error: " << e.what() << std::endl;
    }

    return result;
}

// ============================================================
//  applyNMS —— 按类别分组进行非极大值抑制
// ============================================================
void RobotVision::applyNMS(std::vector<Detection>& detections) {
    if (detections.size() <= 1) return;

    std::sort(detections.begin(), detections.end(),
              [](const Detection& a, const Detection& b) {
                  return a.confidence > b.confidence;
              });

    std::map<int, std::vector<size_t>> class_groups;
    for (size_t i = 0; i < detections.size(); ++i)
        class_groups[detections[i].class_id].push_back(i);

    std::vector<Detection> filtered;
    for (auto& group : class_groups) {
        auto& indices = group.second;
        std::vector<cv::Rect> boxes;
        std::vector<float> confs;
        for (size_t idx : indices) {
            boxes.push_back(detections[idx].bbox);
            confs.push_back(detections[idx].confidence);
        }
        std::vector<int> nms_indices;
        cv::dnn::NMSBoxes(boxes, confs, 0.0f, pImpl->nms_threshold, nms_indices);
        for (int idx : nms_indices)
            filtered.push_back(detections[indices[idx]]);
    }

    std::sort(filtered.begin(), filtered.end(),
              [](const Detection& a, const Detection& b) {
                  return a.confidence > b.confidence;
              });
    detections = filtered;
}

void RobotVision::setConfidenceThreshold(float thresh) {
    if (pImpl) pImpl->conf_threshold = thresh;
}

std::string RobotVision::getBeanName(int class_id) {
    auto it = CLASS_NAMES_MAP.find(class_id);
    return (it != CLASS_NAMES_MAP.end()) ? it->second : "unknown";
}
