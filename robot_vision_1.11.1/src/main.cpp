#include "../include/RobotVision.hpp"
#include "../include/MindVisionCamera.hpp"
#include <cstdlib>
#include <opencv2/opencv.hpp>
#include <iostream>

int main() {
    std::cout << "=== Test Inference ===" << std::endl;
    
    MindVisionCamera camera;
    if (!camera.open(0)) {
        std::cerr << "Failed to open camera!" << std::endl;
        return -1;
    }
    
    // ⭐ 使用环境变量或当前目录获取模型路径
    const char* home = getenv("HOME");
    std::string model_path = home ? std::string(home) + "/robot_vision/models/best.onnx"
                                  : "../models/best.onnx";
    RobotVision vision(model_path);
    
    cv::Mat frame;
    int frame_count = 0;
    while (true) {
        if (!camera.read(frame)) {
            std::cerr << "Failed to read frame!" << std::endl;
            break;
        }
        
        frame_count++;
        std::cout << "Frame " << frame_count << ": running inference..." << std::endl;
        
        auto result = vision.infer(frame);
        
        std::cout << "  Beans: " << result.beans.size() 
                  << ", Digits: " << result.target_digits.size() 
                  << ", Ignore: " << result.ignore_digits.size() << std::endl;
        
        // 绘制豆子（绿色）+ 置信度
        for (const auto& det : result.beans) {
            std::cout << "  Drawing bean: " << det.class_name
                      << " (" << (int)(det.confidence * 100) << "%)" << std::endl;
            cv::rectangle(frame, det.bbox, cv::Scalar(0, 255, 0), 2);
            std::string label = det.class_name + " " + std::to_string((int)(det.confidence * 100)) + "%";
            cv::putText(frame, label,
                       cv::Point(det.bbox.x, det.bbox.y - 10),
                       cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(0, 255, 0), 2);
        }
        // 绘制目标数字（蓝色）+ 置信度
        for (const auto& det : result.target_digits) {
            std::cout << "  Drawing digit: data_" << vision.getDigitValue(det.class_id)
                      << " (" << (int)(det.confidence * 100) << "%)" << std::endl;
            cv::rectangle(frame, det.bbox, cv::Scalar(255, 0, 0), 2);
            std::string label = "data_" + std::to_string(vision.getDigitValue(det.class_id))
                              + " " + std::to_string((int)(det.confidence * 100)) + "%";
            cv::putText(frame, label,
                       cv::Point(det.bbox.x, det.bbox.y - 10),
                       cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(255, 0, 0), 2);
        }
        // 绘制忽略数字（红色）
        for (const auto& det : result.ignore_digits) {
            std::cout << "  Drawing ignore digit: data_" << vision.getDigitValue(det.class_id)
                      << " (" << (int)(det.confidence * 100) << "%)" << std::endl;
            cv::rectangle(frame, det.bbox, cv::Scalar(0, 0, 255), 2);
            std::string label = "IGNORE data_" + std::to_string(vision.getDigitValue(det.class_id))
                              + " " + std::to_string((int)(det.confidence * 100)) + "%";
            cv::putText(frame, label,
                       cv::Point(det.bbox.x, det.bbox.y - 10),
                       cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(0, 0, 255), 2);
        }
        
        cv::imshow("Test", frame);
        if (cv::waitKey(30) == 'q') break;
    }
    
    camera.close();
    return 0;
}