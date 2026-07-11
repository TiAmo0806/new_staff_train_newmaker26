#include "input/InputManager.h"
#include "camera/CameraManager.h"

#include <iostream>
#include <memory>

/**
 * @brief 构造图像输入管理器。
 * @param config 输入配置，决定输入源类型和路径。
 */
InputManager::InputManager(const InputConfig& input_config, const CameraConfig& camera_config)
    : input_config_(input_config), camera_config_(camera_config) {}

/**
 * @brief 析构时释放输入资源。
 */
InputManager::~InputManager() {
    release();
}

/**
 * @brief 打开输入源。
 * @return 打开成功返回 true，失败返回 false。
 */
bool InputManager::open() {
    if (input_config_.type == "mock") {
        // mock 输入不依赖相机，也不依赖图片文件。
        image_ = cv::Mat(720, 1080, CV_8UC3, cv::Scalar(35, 35, 35));
        image_sent_ = false;
        return true;
    }

    if (input_config_.type == "image") {
        // image 模式只读取一张图片，适合调试单帧识别效果。
        image_ = cv::imread(input_config_.source);
        image_sent_ = false;
        if (image_.empty()) {
            std::cerr << "Failed to read image: " << input_config_.source << "\n";
            return false;
        }
        return true;
    }

    if (input_config_.type == "video") {
        // video 模式交给 OpenCV VideoCapture 按帧读取。
        return cap_.open(input_config_.source);
    }

    if (input_config_.type == "camera") {
        if (!cap_.open(camera_config_.camera_id)) {
            std::cerr << "Failed to open camera: " << camera_config_.camera_id << "\n";
            return false;
        }
        cap_.set(cv::CAP_PROP_FRAME_WIDTH, camera_config_.width);
        cap_.set(cv::CAP_PROP_FRAME_HEIGHT, camera_config_.height);
        cap_.set(cv::CAP_PROP_FPS, camera_config_.fps);
        cap_.set(cv::CAP_PROP_AUTO_EXPOSURE, camera_config_.auto_exposure ? 0.75 : 0.25);
        if (!camera_config_.auto_exposure) {
            cap_.set(cv::CAP_PROP_EXPOSURE, camera_config_.exposure_time);
        }
        cap_.set(cv::CAP_PROP_GAIN, camera_config_.gain);
        std::cout << "OpenCV camera opened, id: " << camera_config_.camera_id
                  << " size=" << camera_config_.width << "x" << camera_config_.height
                  << " fps=" << camera_config_.fps << "\n";
        return true;
    }

    if (input_config_.type == "mindvision_camera") {
#ifdef BVP_WITH_MINDVISION
        // 工业相机模式优先使用 MindVision SDK。
        camera_manager_ = std::make_unique<CameraManager>(camera_config_);
        return camera_manager_->open();
#else
        std::cerr << "MindVision camera support is not enabled in this build.\n";
        std::cerr << "Current build cannot open industrial camera with input.type=mindvision_camera.\n";
        std::cerr << "You need a build that links the MindVision SDK instead of falling back to /dev/videoX.\n";
        return false;
#endif
    }

    std::cerr << "Unknown input type: " << input_config_.type << "\n";
    return false;
}

/**
 * @brief 读取一帧图像。
 * @param frame 输出参数，成功时写入当前帧图像。
 * @return 成功读取返回 true；没有更多帧或失败返回 false。
 */
bool InputManager::read(cv::Mat& frame) {
    if (input_config_.type == "mock" || input_config_.type == "image") {
        if (image_.empty() || image_sent_) {
            return false;
        }
        frame = image_.clone();
        image_sent_ = true;
        return true;
    }

    if (input_config_.type == "camera") {
        return cap_.read(frame);
    }

    if (input_config_.type == "mindvision_camera") {
#ifdef BVP_WITH_MINDVISION
        return camera_manager_ != nullptr && camera_manager_->read(frame);
#else
        return false;
#endif
    }

    // video 模式下，每次 read 都尝试读取下一帧。
    return cap_.read(frame);
}

/**
 * @brief 释放输入资源。
 */
void InputManager::release() {
    if (cap_.isOpened()) {
        cap_.release();
    }
#ifdef BVP_WITH_MINDVISION
    if (camera_manager_) {
        camera_manager_->close();
        camera_manager_.reset();
    }
#endif
}
