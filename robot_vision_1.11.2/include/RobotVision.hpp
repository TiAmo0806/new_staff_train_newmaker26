#ifndef ROBOT_VISION_HPP
#define ROBOT_VISION_HPP

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <map>
#include <mutex>
#include <memory>
#include <onnxruntime_cxx_api.h>

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

    // 内置类别名称表（供外部查询）
    static const std::map<int, std::string>& getClassNames();

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;

    inline bool isBean(int class_id) { return class_id >= 0 && class_id <= 2; }
    inline bool isDigit(int class_id) { return class_id >= 3 && class_id <= 7; }
    inline bool isTargetDigit(int class_id) { return class_id >= 3 && class_id <= 5; }
    inline bool isIgnoreDigit(int class_id) { return class_id >= 6 && class_id <= 7; }

public:
    RobotVision() = default;
    explicit RobotVision(const std::string& model_path, float conf_thresh = 0.3);
    ~RobotVision();

    void loadModel(const std::string& model_path, float conf_thresh = 0.3);
    ClassificationResult infer(const cv::Mat& frame);
    int getDigitValue(int class_id) { return class_id - 2; }
    std::string getBeanName(int class_id);
    void setConfidenceThreshold(float thresh);

private:
    struct LetterBoxInfo {
        float scale = 1.0f;
        int pad_left = 0;
        int pad_top = 0;
    };

    cv::Mat preprocess(const cv::Mat& frame, LetterBoxInfo& lb_info);
    void applyNMS(std::vector<Detection>& detections);
};

#endif