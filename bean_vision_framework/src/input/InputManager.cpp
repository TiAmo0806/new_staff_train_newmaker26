#include "input/InputManager.h"

#ifdef BVP_WITH_MINDVISION
#include <CameraApi.h>
#endif

#include <iostream>
#include <memory>

#ifdef BVP_WITH_MINDVISION

/**
 * @brief 迈德威视工业相机输入封装。
 *
 * 这段初始化和取帧流程来自已经验证通过的 cpp_camera_mindvision_ort 示例。
 */
class MindVisionCamera {
public:
    MindVisionCamera() = default;

    /**
     * @brief 打开指定索引的迈德威视相机。
     * @param camera_index 相机索引，通常第一台为 0。
     * @return 打开成功返回 true。
     */
    bool open(int camera_index) {
        CameraSdkInit(1);

        int camera_count = 16;
        tSdkCameraDevInfo camera_list[16];
        if (CameraEnumerateDevice(camera_list, &camera_count) != CAMERA_STATUS_SUCCESS || camera_count <= 0) {
            std::cerr << "No MindVision camera found.\n";
            return false;
        }

        if (camera_index < 0 || camera_index >= camera_count) {
            std::cerr << "MindVision camera index is out of range. Found " << camera_count << " camera(s).\n";
            return false;
        }

        if (CameraInit(&camera_list[camera_index], -1, -1, &camera_) != CAMERA_STATUS_SUCCESS) {
            std::cerr << "MindVision camera initialization failed.\n";
            return false;
        }

        if (CameraGetCapability(camera_, &capability_) != CAMERA_STATUS_SUCCESS) {
            std::cerr << "Failed to get MindVision camera capability.\n";
            close();
            return false;
        }

        // 输出 BGR8，后续 OpenCV/YOLO 流程可以直接使用。
        CameraSetIspOutFormat(camera_, CAMERA_MEDIA_TYPE_BGR8);
        CameraSetMirror(camera_, MIRROR_DIRECTION_HORIZONTAL, FALSE);
        CameraSetMirror(camera_, MIRROR_DIRECTION_VERTICAL, FALSE);
        CameraSetHardwareMirror(camera_, MIRROR_DIRECTION_HORIZONTAL, FALSE);
        CameraSetHardwareMirror(camera_, MIRROR_DIRECTION_VERTICAL, FALSE);
        CameraSetTriggerMode(camera_, 0);
        CameraPlay(camera_);

        const int buffer_size =
            capability_.sResolutionRange.iWidthMax * capability_.sResolutionRange.iHeightMax * 3;
        rgb_buffer_.reset(new unsigned char[buffer_size]);

        std::cout << "MindVision camera opened, index: " << camera_index << "\n";
        return true;
    }

    /**
     * @brief 从相机读取一帧 BGR 图像。
     * @param frame 输出图像。
     * @return 读取成功返回 true。
     */
    bool read(cv::Mat& frame) {
        if (camera_ < 0) {
            return false;
        }

        tSdkFrameHead frame_info;
        BYTE* raw_buffer = nullptr;
        if (CameraGetImageBuffer(camera_, &frame_info, &raw_buffer, 1000) != CAMERA_STATUS_SUCCESS) {
            return false;
        }

        CameraImageProcess(camera_, raw_buffer, rgb_buffer_.get(), &frame_info);
        CameraReleaseImageBuffer(camera_, raw_buffer);

        cv::Mat image(frame_info.iHeight, frame_info.iWidth, CV_8UC3, rgb_buffer_.get());
        frame = image.clone();
        return true;
    }

    /**
     * @brief 关闭相机并释放缓存。
     */
    void close() {
        if (camera_ >= 0) {
            CameraUnInit(camera_);
            camera_ = -1;
        }
        rgb_buffer_.reset();
    }

    ~MindVisionCamera() {
        close();
    }

private:
    CameraHandle camera_{-1};
    tSdkCameraCapbility capability_{};
    std::unique_ptr<unsigned char[]> rgb_buffer_;
};

#endif

/**
 * @brief 构造图像输入管理器。
 * @param config 输入配置，决定输入源类型和路径。
 */
InputManager::InputManager(const InputConfig& config) : config_(config) {}

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
    if (config_.type == "mock") {
        // mock 输入不依赖相机，也不依赖图片文件。
        image_ = cv::Mat(720, 1080, CV_8UC3, cv::Scalar(35, 35, 35));
        image_sent_ = false;
        return true;
    }

    if (config_.type == "image") {
        // image 模式只读取一张图片，适合调试单帧识别效果。
        image_ = cv::imread(config_.source);
        image_sent_ = false;
        if (image_.empty()) {
            std::cerr << "Failed to read image: " << config_.source << "\n";
            return false;
        }
        return true;
    }

    if (config_.type == "video") {
        // video 模式交给 OpenCV VideoCapture 按帧读取。
        return cap_.open(config_.source);
    }

    if (config_.type == "camera") {
        if (!cap_.open(config_.camera_id)) {
            std::cerr << "Failed to open camera: " << config_.camera_id << "\n";
            return false;
        }
        cap_.set(cv::CAP_PROP_FRAME_WIDTH, config_.width);
        cap_.set(cv::CAP_PROP_FRAME_HEIGHT, config_.height);
        cap_.set(cv::CAP_PROP_FPS, config_.fps);
        cap_.set(cv::CAP_PROP_AUTO_EXPOSURE, config_.auto_exposure ? 0.75 : 0.25);
        if (!config_.auto_exposure) {
            cap_.set(cv::CAP_PROP_EXPOSURE, static_cast<double>(config_.exposure));
        }
        cap_.set(cv::CAP_PROP_GAIN, static_cast<double>(config_.gain));
        std::cout << "OpenCV camera opened, id: " << config_.camera_id
                  << " size=" << config_.width << "x" << config_.height
                  << " fps=" << config_.fps << "\n";
        return true;
    }

    if (config_.type == "mindvision_camera") {
#ifdef BVP_WITH_MINDVISION
        // 工业相机模式优先使用 MindVision SDK。
        mindvision_camera_ = std::make_unique<MindVisionCamera>();
        return mindvision_camera_->open(config_.camera_id);
#else
        std::cerr << "MindVision camera support is not enabled in this build.\n";
        std::cerr << "Current build cannot open industrial camera with input.type=mindvision_camera.\n";
        std::cerr << "You need a build that links the MindVision SDK instead of falling back to /dev/videoX.\n";
        return false;
#endif
    }

    std::cerr << "Unknown input type: " << config_.type << "\n";
    return false;
}

/**
 * @brief 读取一帧图像。
 * @param frame 输出参数，成功时写入当前帧图像。
 * @return 成功读取返回 true；没有更多帧或失败返回 false。
 */
bool InputManager::read(cv::Mat& frame) {
    if (config_.type == "mock" || config_.type == "image") {
        if (image_.empty() || image_sent_) {
            return false;
        }
        frame = image_.clone();
        image_sent_ = true;
        return true;
    }

    if (config_.type == "camera") {
        return cap_.read(frame);
    }

    if (config_.type == "mindvision_camera") {
#ifdef BVP_WITH_MINDVISION
        return mindvision_camera_ != nullptr && mindvision_camera_->read(frame);
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
    if (mindvision_camera_) {
        mindvision_camera_->close();
        mindvision_camera_.reset();
    }
#endif
}
