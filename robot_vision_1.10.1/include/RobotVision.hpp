#ifndef ROBOT_VISION_HPP
#define ROBOT_VISION_HPP

#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include <vector>
#include <string>
#include <map>
#include <mutex>

class RobotVision {
public:
    struct Detection {
        int class_id;
        std::string class_name;
        float confidence;
        cv::Rect bbox;
        cv::Point2f center;
    };

    struct ClassificationResult {
        std::vector<Detection> beans;
        std::vector<Detection> digits;
        std::vector<Detection> target_digits;
        std::vector<Detection> ignore_digits;
    };

private:
    cv::dnn::Net net;
    int input_width = 640;
    int input_height = 640;
    float conf_threshold = 0.45;
    float nms_threshold = 0.4;
    std::mutex model_mutex;

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

    inline bool isBean(int class_id) { return class_id >= 0 && class_id <= 2; }
    inline bool isDigit(int class_id) { return class_id >= 3 && class_id <= 7; }
    inline bool isTargetDigit(int class_id) { return class_id >= 3 && class_id <= 5; }
    inline bool isIgnoreDigit(int class_id) { return class_id >= 6 && class_id <= 7; }

public:
    RobotVision() = default;
    explicit RobotVision(const std::string& model_path, float conf_thresh = 0.45);
    void loadModel(const std::string& model_path, float conf_thresh = 0.45);
    ClassificationResult infer(const cv::Mat& frame);
    int getDigitValue(int class_id) { return class_id - 2; }
    std::string getBeanName(int class_id) { return class_names[class_id]; }

private:
    cv::Mat preprocess(const cv::Mat& frame);
    std::vector<Detection> postprocess(const cv::Mat& output, const cv::Size& original_size);
    void applyNMS(std::vector<Detection>& detections);
};

#endif
