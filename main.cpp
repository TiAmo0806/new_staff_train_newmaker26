#include "../include/VisionController.hpp"
#include <opencv2/opencv.hpp>
#include <iostream>
#include <signal.h>
#include <atomic>
#include <cstdlib>

std::atomic<bool> running{true};

void signalHandler(int signum) {
    std::cout << "\nShutting down..." << std::endl;
    running = false;
}

int main(int argc, char** argv) {
    signal(SIGINT, signalHandler);
    
    const char* home = getenv("HOME");
    std::string model_path = std::string(home) + "/robot_vision/models/best.onnx";
    std::string serial_port = "/dev/ttyUSB0";
    int camera_id = 0;
    
    if (argc >= 2) model_path = argv[1];
    if (argc >= 3) serial_port = argv[2];
    if (argc >= 4) camera_id = std::stoi(argv[3]);
    
    std::cout << "=== Robot Vision System ===" << std::endl;
    std::cout << "Model: " << model_path << std::endl;
    std::cout << "Serial: " << serial_port << std::endl;
    std::cout << "Camera: " << camera_id << std::endl;
    
    cv::VideoCapture cap(camera_id);
    if (!cap.isOpened()) {
        std::cerr << "Failed to open camera!" << std::endl;
        return -1;
    }
    
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
    
    try {
        VisionController controller(model_path, serial_port);
        cv::Mat frame;
        while (running) {
            cap >> frame;
            if (frame.empty()) break;
            controller.processFrame(frame);
            if (cv::waitKey(1) == 'q') break;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return -1;
    }
    
    cap.release();
    cv::destroyAllWindows();
    return 0;
}