#pragma once

#include "core/AppConfig.h"

#include <opencv2/opencv.hpp>

#include <memory>

#ifdef BVP_WITH_MINDVISION
class CameraManager;
#endif

enum class ImagePurpose {
    Default,
    Beans,
    Digits
};

class InputManager {
public:
    /**
     * @brief 构造图像输入管理器。
     * @param input_config 输入配置，决定使用 mock、image、video 或 mindvision_camera。
     * @param camera_config 相机参数配置。
     */
    InputManager(const InputConfig& input_config, const CameraConfig& camera_config);

    /**
     * @brief 析构时释放输入资源。
     */
    ~InputManager();

    /**
     * @brief 打开输入源。
     * @return 打开成功返回 true，失败返回 false。
     *
     * mock 模式会创建一张空白图；image 模式会读取单张图片；
     * video 模式会打开 OpenCV VideoCapture；
     * camera/mindvision_camera 模式会使用迈德威视 SDK 打开工业相机。
     */
    bool open();

    /**
     * @brief 读取一帧图像。
     * @param frame 输出参数，成功时保存读取到的图像。
     * @return 成功读取一帧返回 true；没有更多帧或读取失败返回 false。
     */
    bool read(cv::Mat& frame);

    /**
     * @brief 在 image 模式下切换到豆子阶段专用图片。
     * @return 切换成功返回 true；非 image 模式下直接返回 true。
     */
    bool selectImageForBeans();

    /**
     * @brief 在 image 模式下切换到数字阶段专用图片。
     * @return 切换成功返回 true；非 image 模式下直接返回 true。
     */
    bool selectImageForDigits();

    /**
     * @brief 重置输入源的读取状态。
     *
     * image/mock 模式下会重置单次读取标志，使下一次 read() 可以再次成功；
     * video 模式会尽量回到第一帧；
     * camera/mindvision_camera 模式只清理本地读取状态，不关闭设备。
     */
    void reset();

    /**
     * @brief 释放输入资源。
     *
     * 对 video 模式会释放 VideoCapture；
     * 对 camera/mindvision_camera 模式会释放工业相机句柄。
     */
    void release();

private:
    bool selectImageSource(ImagePurpose purpose);
    bool loadImageFile(const std::string& path);

    InputConfig input_config_;
    CameraConfig camera_config_;
    cv::VideoCapture cap_;
    cv::Mat image_;
    std::string current_image_path_;
#ifdef BVP_WITH_MINDVISION
    std::unique_ptr<CameraManager> camera_manager_;
#endif
    bool image_sent_ = false;
};
