#include "../include/RobotVision.hpp"
#include <algorithm>
#include <iostream>

RobotVision::RobotVision(const std::string& model_path, float conf_thresh) {
    loadModel(model_path, conf_thresh);
}

void RobotVision::loadModel(const std::string& model_path, float conf_thresh) {
    std::lock_guard<std::mutex> lock(model_mutex);
    conf_threshold = conf_thresh;
    
    try {
        net = cv::dnn::readNetFromONNX(model_path);
        net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
        std::cout << "[Vision] Model loaded: " << model_path << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[Vision] Failed to load model: " << e.what() << std::endl;
        throw;
    }
}

cv::Mat RobotVision::preprocess(const cv::Mat& frame) {
    cv::Mat blob;
    cv::dnn::blobFromImage(frame, blob, 1.0/255.0, 
                          cv::Size(input_width, input_height), 
                          cv::Scalar(), true, false);
    return blob;
}

std::vector<RobotVision::Detection> RobotVision::postprocess(
    const cv::Mat& output, 
    const cv::Size& original_size) {
    
    std::vector<Detection> detections;
    const int num_classes = 8;
    const int num_anchors = output.size[2];
    
    if (output.dims == 3 && output.size[0] == 1) {
        const float* data = (float*)output.data;
        
        for (int i = 0; i < num_anchors; i++) {
            const float* row = data + i * (num_classes + 4 + 1);
            
            float cx = row[0];
            float cy = row[1];
            float w = row[2];
            float h = row[3];
            
            int class_id = -1;
            float max_conf = 0.0f;
            
            for (int j = 0; j < num_classes; j++) {
                float prob = row[5 + j];
                if (prob > max_conf) {
                    max_conf = prob;
                    class_id = j;
                }
            }
            
            if (max_conf >= conf_threshold && class_id >= 0) {
                Detection det;
                det.class_id = class_id;
                det.class_name = class_names[class_id];
                det.confidence = max_conf;
                
                float x = (cx - w/2) * original_size.width;
                float y = (cy - h/2) * original_size.height;
                float width = w * original_size.width;
                float height = h * original_size.height;
                
                det.bbox = cv::Rect(std::max(0, (int)x), std::max(0, (int)y), 
                                   (int)width, (int)height);
                det.center = cv::Point2f(cx * original_size.width, cy * original_size.height);
                detections.push_back(det);
            }
        }
    }
    
    applyNMS(detections);
    return detections;
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
    cv::dnn::NMSBoxes(boxes, confidences, conf_threshold, nms_threshold, indices);
    
    std::vector<Detection> filtered;
    for (int idx : indices) {
        filtered.push_back(detections[idx]);
    }
    detections = filtered;
}

RobotVision::ClassificationResult RobotVision::infer(const cv::Mat& frame) {
    std::lock_guard<std::mutex> lock(model_mutex);
    ClassificationResult result;
    
    if (frame.empty()) return result;
    
    try {
        cv::Mat blob = preprocess(frame);
        net.setInput(blob);
        cv::Mat output = net.forward();
        std::vector<Detection> all_detections = postprocess(output, frame.size());
        
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
