#include "../include/RobotVision.hpp"
#include "../include/MindVisionCamera.hpp"
#include <opencv2/opencv.hpp>
#include <iostream>

int main() {
    std::cout << "=== Test Inference ===" << std::endl;
    
    MindVisionCamera camera;
    if (!camera.open(0)) {
        std::cerr << "Failed to open camera!" << std::endl;
        return -1;
    }
    
    RobotVision vision("/home/c349/robot_vision/models/best.onnx");
    
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
        
        // 绘制豆子（绿色）
        for (const auto& det : result.beans) {
            std::cout << "  Drawing bean: " << det.class_name << std::endl;
            cv::rectangle(frame, det.bbox, cv::Scalar(0, 255, 0), 2);
            cv::putText(frame, det.class_name, 
                       cv::Point(det.bbox.x, det.bbox.y - 10),
                       cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 2);
        }
        // 绘制数字（蓝色）
        for (const auto& det : result.target_digits) {
            std::cout << "  Drawing digit: data_" << vision.getDigitValue(det.class_id) << std::endl;
            cv::rectangle(frame, det.bbox, cv::Scalar(255, 0, 0), 2);
            std::string label = "data_" + std::to_string(vision.getDigitValue(det.class_id));
            cv::putText(frame, label, 
                       cv::Point(det.bbox.x, det.bbox.y - 10),
                       cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 0, 0), 2);
        }
        // 绘制忽略数字（红色）
        for (const auto& det : result.ignore_digits) {
            std::cout << "  Drawing ignore digit: data_" << vision.getDigitValue(det.class_id) << std::endl;
            cv::rectangle(frame, det.bbox, cv::Scalar(0, 0, 255), 2);
            cv::putText(frame, "IGNORE", 
                       cv::Point(det.bbox.x, det.bbox.y - 10),
                       cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 255), 2);
        }
        
        cv::imshow("Test", frame);
        if (cv::waitKey(30) == 'q') break;
    }
    
    camera.close();
    return 0;
}