#include "../include/RobotVision.hpp"
#include <algorithm>
#include <iostream>
#include <openvino/openvino.hpp>
#include <vector>
#include <cstring>

class RobotVision::Impl {
public:
    ov::Core core;
    ov::CompiledModel compiled_model;
    ov::InferRequest infer_request;
    
    std::vector<ov::Output<ov::Node>> input_ports;
    std::vector<ov::Output<ov::Node>> output_ports;
    
    int input_width = 640;
    int input_height = 640;
    float conf_threshold = 0.45;
    float nms_threshold = 0.4;
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

cv::Mat RobotVision::preprocess(const cv::Mat& frame) {
    cv::Mat resized;
    cv::resize(frame, resized, cv::Size(pImpl->input_width, pImpl->input_height));
    resized.convertTo(resized, CV_32FC3, 1.0 / 255.0);
    return resized;
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
        cv::Mat preprocessed = preprocess(frame);
        
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
        
        std::cout << "[Vision] Output shape: ";
        for (auto dim : output_shape) {
            std::cout << dim << " ";
        }
        std::cout << std::endl;
        
        int num_detections = output_shape[2];
        int num_classes = 8;
        int stride = 12;
        
        std::cout << "[Vision] num_detections: " << num_detections << std::endl;
        
        std::vector<Detection> all_detections;
        
        // ⭐ 打印前5个检测框的原始数据
        std::cout << "[Vision] First 5 raw detections (cx, cy, w, h, cls...):" << std::endl;
        for (int i = 0; i < std::min(5, num_detections); i++) {
            const float* row = output_data + i * stride;
            std::cout << "  " << i << ": ";
            for (int j = 0; j < 12; j++) {
                std::cout << row[j] << " ";
            }
            std::cout << std::endl;
        }
        
        // ⭐ 打印所有检测到的目标（筛选后的）
        std::cout << "[Vision] Detected targets:" << std::endl;
        
        for (int i = 0; i < num_detections; i++) {
            const float* row = output_data + i * stride;
            
            float cx = row[0];
            float cy = row[1];
            float w = row[2];
            float h = row[3];
            
            // 坐标有效性检查
            if (cx < 0 || cx > 1 || cy < 0 || cy > 1 || w < 0 || w > 1 || h < 0 || h > 1) {
                continue;
            }
            
            int class_id = -1;
            float max_conf = 0.0f;
            for (int j = 0; j < num_classes; j++) {
                float prob = row[4 + j];
                if (prob > max_conf) {
                    max_conf = prob;
                    class_id = j;
                }
            }
            
            if (max_conf < pImpl->conf_threshold || class_id < 0) continue;
            
            // ⭐ 打印检测到的目标信息
            std::cout << "  Target: class=" << class_id 
                      << " conf=" << max_conf
                      << " cx=" << cx << " cy=" << cy 
                      << " w=" << w << " h=" << h << std::endl;
            
            Detection det;
            det.class_id = class_id;
            det.class_name = pImpl->class_names[class_id];
            det.confidence = max_conf;
            
            // ⭐ 坐标转换：尝试不同的转换方式
            // 方式1: 标准 YOLO 归一化坐标
            float x1 = (cx - w/2) * frame.cols;
            float y1 = (cy - h/2) * frame.rows;
            float width1 = w * frame.cols;
            float height1 = h * frame.rows;
            
            // ⭐ 方式2: 如果坐标已经是像素值（不归一化）
            float x2 = cx - w/2;
            float y2 = cy - h/2;
            float width2 = w;
            float height2 = h;
            
            // 判断使用哪种方式：如果 cx > 1，说明是像素坐标
            if (cx > 1 || cy > 1) {
                // 使用方式2（像素坐标）
                det.bbox = cv::Rect(std::max(0, (int)x2), std::max(0, (int)y2), 
                                   (int)width2, (int)height2);
                std::cout << "    Using pixel coords: " << det.bbox.x << "," << det.bbox.y 
                          << " " << det.bbox.width << "x" << det.bbox.height << std::endl;
            } else {
                // 使用方式1（归一化坐标）
                det.bbox = cv::Rect(std::max(0, (int)x1), std::max(0, (int)y1), 
                                   (int)width1, (int)height1);
                std::cout << "    Using normalized coords: " << det.bbox.x << "," << det.bbox.y 
                          << " " << det.bbox.width << "x" << det.bbox.height << std::endl;
            }
            
            det.center = cv::Point2f(cx * frame.cols, cy * frame.rows);
            
            all_detections.push_back(det);
        }
        
        std::cout << "[Vision] Detections before NMS: " << all_detections.size() << std::endl;
        
        applyNMS(all_detections);
        
        std::cout << "[Vision] Detections after NMS: " << all_detections.size() << std::endl;
        
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
    
    std::sort(detections.begin(), detections.end(),
              [](const Detection& a, const Detection& b) {
                  return a.confidence > b.confidence;
              });
    
    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> class_ids;
    
    for (const auto& det : detections) {
        boxes.push_back(det.bbox);
        confidences.push_back(det.confidence);
        class_ids.push_back(det.class_id);
    }
    
    std::vector<int> indices;
    cv::dnn::NMSBoxes(boxes, confidences, pImpl->conf_threshold, pImpl->nms_threshold, indices);
    
    std::vector<Detection> filtered;
    for (int idx : indices) {
        filtered.push_back(detections[idx]);
    }
    detections = filtered;
}

void RobotVision::setConfidenceThreshold(float thresh) {
    if (pImpl) {
        pImpl->conf_threshold = thresh;
    }
}