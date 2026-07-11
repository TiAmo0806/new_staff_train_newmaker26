#include "../include/RobotVision.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <openvino/openvino.hpp>
#include <vector>
#include <string>

class RobotVision::Impl {
public:
    ov::Core core;
    ov::CompiledModel compiled_model;
    ov::InferRequest infer_request;

    std::vector<ov::Output<ov::Node>> input_ports;
    std::vector<ov::Output<ov::Node>> output_ports;

    int input_width = 640;
    int input_height = 640;
    float conf_threshold = 0.3;
    float nms_threshold = 0.3;
    std::mutex model_mutex;
    bool model_loaded = false;

    std::map<int, std::string> class_names = {
        {0, "soybean"},
        {1, "mung_bean"},
        {2, "white_kidney_bean"},
        {3, "data_1"},
        {4, "data_2"},
        {5, "data_3"},
        {6, "data_4"},
        {7, "data_5"}
    };
};

RobotVision::RobotVision(const std::string& model_path, float conf_thresh)
    : pImpl(std::make_unique<Impl>()) {
    loadModel(model_path, conf_thresh);
}

RobotVision::~RobotVision() = default;

void RobotVision::loadModel(const std::string& model_path, float conf_thresh) {
    std::lock_guard<std::mutex> lock(pImpl->model_mutex);
    pImpl->conf_threshold = conf_thresh;

    std::cout << "[Vision] loadModel() started for: " << model_path << std::endl;

    try {
        std::cout << "[Vision] Reading model..." << std::endl;
        auto model = pImpl->core.read_model(model_path);
        std::cout << "[Vision] Model read successfully!" << std::endl;

        pImpl->input_ports = model->inputs();
        pImpl->output_ports = model->outputs();

        std::cout << "[Vision] Number of inputs: " << pImpl->input_ports.size() << std::endl;
        for (size_t i = 0; i < pImpl->input_ports.size(); i++) {
            auto shape = pImpl->input_ports[i].get_shape();
            std::cout << "[Vision] Input " << i << " shape: ";
            for (auto dim : shape) {
                std::cout << dim << " ";
            }
            std::cout << std::endl;
        }

        std::cout << "[Vision] Number of outputs: " << pImpl->output_ports.size() << std::endl;
        for (size_t i = 0; i < pImpl->output_ports.size(); i++) {
            auto shape = pImpl->output_ports[i].get_shape();
            std::cout << "[Vision] Output " << i << " shape: ";
            for (auto dim : shape) {
                std::cout << dim << " ";
            }
            std::cout << std::endl;
        }

        if (!pImpl->input_ports.empty()) {
            auto shape = pImpl->input_ports[0].get_shape();
            if (shape.size() >= 4) {
                pImpl->input_height = shape[2];
                pImpl->input_width = shape[3];
            }
        }

        std::cout << "[Vision] Compiling model..." << std::endl;
        pImpl->compiled_model = pImpl->core.compile_model(model, "CPU");
        std::cout << "[Vision] Model compiled successfully!" << std::endl;

        pImpl->infer_request = pImpl->compiled_model.create_infer_request();
        std::cout << "[Vision] Infer request created!" << std::endl;

        pImpl->model_loaded = true;
        std::cout << "[Vision] Model loaded successfully!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[Vision] loadModel() exception: " << e.what() << std::endl;
        pImpl->model_loaded = false;
        throw;
    }
}

// ============================================================
// 预处理: Letterbox + 归一化
// 保持宽高比，用灰色填充，避免直接 resize 导致的变形
// 返回 letterbox 信息用于后续坐标还原
// ============================================================
cv::Mat RobotVision::preprocess(const cv::Mat& frame, LetterBoxInfo& lb_info) {
    int target_w = pImpl->input_width;
    int target_h = pImpl->input_height;

    // 计算缩放比例（保持宽高比）
    float scale = std::min((float)target_w / frame.cols, (float)target_h / frame.rows);
    int new_w = (int)(frame.cols * scale);
    int new_h = (int)(frame.rows * scale);

    // 计算填充偏移（居中放置）
    int pad_left = (target_w - new_w) / 2;
    int pad_top = (target_h - new_h) / 2;

    // 保存 letterbox 参数用于坐标还原
    lb_info.scale = scale;
    lb_info.pad_left = pad_left;
    lb_info.pad_top = pad_top;

    // 缩放到新尺寸
    cv::Mat resized;
    cv::resize(frame, resized, cv::Size(new_w, new_h));

    // 创建灰色画布并居中放置
    cv::Mat canvas(target_h, target_w, CV_8UC3, cv::Scalar(114, 114, 114));
    resized.copyTo(canvas(cv::Rect(pad_left, pad_top, new_w, new_h)));

    // 归一化到 [0, 1]
    cv::Mat float_mat;
    canvas.convertTo(float_mat, CV_32FC3, 1.0 / 255.0);
    return float_mat;
}

RobotVision::ClassificationResult RobotVision::infer(const cv::Mat& frame) {
    ClassificationResult result;

    if (!pImpl->model_loaded) {
        std::cerr << "[Vision] Model not loaded properly!" << std::endl;
        return result;
    }

    std::lock_guard<std::mutex> lock(pImpl->model_mutex);

    if (frame.empty()) return result;

    try {
        LetterBoxInfo lb_info;
        cv::Mat preprocessed = preprocess(frame, lb_info);

        std::vector<float> input_data;
        input_data.reserve(pImpl->input_width * pImpl->input_height * 3);

        std::vector<cv::Mat> channels(3);
        cv::split(preprocessed, channels);

        for (int c = 0; c < 3; c++) {
            for (int h = 0; h < pImpl->input_height; h++) {
                for (int w = 0; w < pImpl->input_width; w++) {
                    input_data.push_back(channels[c].at<float>(h, w));
                }
            }
        }

        auto input_tensor = ov::Tensor(pImpl->input_ports[0].get_element_type(),
                                       pImpl->input_ports[0].get_shape(),
                                       input_data.data());
        pImpl->infer_request.set_input_tensor(input_tensor);

        pImpl->infer_request.infer();

        auto output_tensor = pImpl->infer_request.get_output_tensor(0);
        float* output_data = output_tensor.data<float>();
        ov::Shape output_shape = output_tensor.get_shape();

        // ============================================================
        // ⭐ 动态解析 YOLOv8 输出格式: [1, 4+num_classes, num_detections]
        // 不再硬编码 num_classes 和 stride
        // ============================================================
        if (output_shape.size() < 3) {
            std::cerr << "[Vision] Unexpected output shape! Expected [1, C, N] got ";
            for (auto d : output_shape) std::cerr << d << " ";
            std::cerr << std::endl;
            return result;
        }

        // ============================================================
        // ⭐ 模型输出布局: [1, C, N] = [1, 12, 8400]
        //    注意: 是 channel-first 布局, 不是 detection-first！
        //    检测 i 的通道 c 位于: output_data[c * num_detections + i]
        // ============================================================
        int total_channels = static_cast<int>(output_shape[1]);  // 4 + num_classes
        int num_classes = total_channels - 4;                     // 动态类别数
        int num_detections = static_cast<int>(output_shape[2]);   // 总检测数

        if (num_classes < 1 || num_classes > 100) {
            std::cerr << "[Vision] Invalid num_classes: " << num_classes
                      << " (channels=" << total_channels << ")" << std::endl;
            return result;
        }

        std::cout << "[Vision] Output: " << num_classes << " classes, "
                  << num_detections << " detections" << std::endl;

        // ============================================================
        // 遍历所有 8400 个检测，读取原始坐标
        // ============================================================
        std::vector<Detection> all_detections;
        const int IMG_W = pImpl->input_width;
        const int IMG_H = pImpl->input_height;

        for (int i = 0; i < num_detections; i++) {

            // --------------------------------------------------
            // 1. 读取原始坐标（模型输出已包含完整解码，无需 grid/stride 变换）
            //    [1, C, N] 布局: offset = c * num_detections + i
            // --------------------------------------------------
            float cx = output_data[0 * num_detections + i];
            float cy = output_data[1 * num_detections + i];
            float w  = output_data[2 * num_detections + i];
            float h  = output_data[3 * num_detections + i];

            // --------------------------------------------------
            // 2. 找到最高分的类别（模型输出已包含 sigmoid）
            // --------------------------------------------------
            float max_conf = 0.0f;
            int best_class = -1;

            for (int c = 0; c < num_classes; c++) {
                // ⭐ 模型导出时已包含 sigmoid，直接取模型输出的置信度
                float conf = output_data[(4 + c) * num_detections + i];
                if (conf > max_conf) {
                    max_conf = conf;
                    best_class = c;
                }
            }

            // 置信度过滤
            if (best_class < 0 || max_conf < pImpl->conf_threshold) {
                continue;
            }

            // --------------------------------------------------
            // 3. 转为角点格式并裁剪到模型输入边界
            // --------------------------------------------------
            float x1 = cx - w / 2.0f;
            float y1 = cy - h / 2.0f;
            float x2 = cx + w / 2.0f;
            float y2 = cy + h / 2.0f;

            // 裁剪到模型输入边界
            if (x1 < 0) x1 = 0;
            if (y1 < 0) y1 = 0;
            if (x2 > IMG_W) x2 = IMG_W;
            if (y2 > IMG_H) y2 = IMG_H;

            // 过滤无效框
            if (x2 <= x1 || y2 <= y1) continue;

            Detection det;
            det.class_id = best_class;
            det.class_name = pImpl->class_names[best_class];
            det.confidence = max_conf;

            // --------------------------------------------------
            // ⭐ 4. 坐标还原：从 letterbox 坐标系 → 原始帧坐标系
            //    1) 去掉 padding
            //    2) 除以 scale 还原到原始尺寸
            // --------------------------------------------------
            float orig_x1 = (x1 - lb_info.pad_left) / lb_info.scale;
            float orig_y1 = (y1 - lb_info.pad_top) / lb_info.scale;
            float orig_x2 = (x2 - lb_info.pad_left) / lb_info.scale;
            float orig_y2 = (y2 - lb_info.pad_top) / lb_info.scale;

            // 裁剪到原始帧边界
            if (orig_x1 < 0) orig_x1 = 0;
            if (orig_y1 < 0) orig_y1 = 0;
            if (orig_x2 > frame.cols) orig_x2 = frame.cols;
            if (orig_y2 > frame.rows) orig_y2 = frame.rows;

            if (orig_x2 <= orig_x1 || orig_y2 <= orig_y1) continue;

            det.bbox = cv::Rect(
                (int)orig_x1,
                (int)orig_y1,
                (int)(orig_x2 - orig_x1),
                (int)(orig_y2 - orig_y1)
            );
            det.center = cv::Point2f(
                (cx - lb_info.pad_left) / lb_info.scale,
                (cy - lb_info.pad_top) / lb_info.scale
            );

            all_detections.push_back(det);
        }

        std::cout << "[Vision] Detections before NMS: " << all_detections.size() << std::endl;

        applyNMS(all_detections);

        std::cout << "[Vision] After NMS: " << all_detections.size() << " detections" << std::endl;

        for (const auto& det : all_detections) {
            if (isBean(det.class_id)) {
                result.beans.push_back(det);
            } else if (isDigit(det.class_id)) {
                result.digits.push_back(det);
                if (isTargetDigit(det.class_id)) {
                    result.target_digits.push_back(det);
                } else if (isIgnoreDigit(det.class_id)) {
                    result.ignore_digits.push_back(det);
                }
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "[Vision] Inference error: " << e.what() << std::endl;
    }

    return result;
}

void RobotVision::applyNMS(std::vector<Detection>& detections) {
    if (detections.size() <= 1) return;

    // ⭐ 先按置信度降序排列
    std::sort(detections.begin(), detections.end(),
              [](const Detection& a, const Detection& b) {
                  return a.confidence > b.confidence;
              });

    // ⭐ 按类别分组，分别进行 NMS，避免不同类别之间互相抑制
    std::map<int, std::vector<size_t>> class_groups;
    for (size_t i = 0; i < detections.size(); i++) {
        class_groups[detections[i].class_id].push_back(i);
    }

    std::vector<Detection> filtered;
    for (auto& [class_id, indices] : class_groups) {
        std::vector<cv::Rect> boxes;
        std::vector<float> confidences;

        for (size_t idx : indices) {
            boxes.push_back(detections[idx].bbox);
            confidences.push_back(detections[idx].confidence);
        }

        std::vector<int> nms_indices;
        cv::dnn::NMSBoxes(boxes, confidences, pImpl->conf_threshold, pImpl->nms_threshold, nms_indices);

        for (int nms_idx : nms_indices) {
            filtered.push_back(detections[indices[nms_idx]]);
        }
    }

    // ⭐ NMS 后再次按置信度排序
    std::sort(filtered.begin(), filtered.end(),
              [](const Detection& a, const Detection& b) {
                  return a.confidence > b.confidence;
              });

    detections = filtered;
}

void RobotVision::setConfidenceThreshold(float thresh) {
    if (pImpl) {
        pImpl->conf_threshold = thresh;
    }
}
