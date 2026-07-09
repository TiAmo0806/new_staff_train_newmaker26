#pragma once

#include "core/AppConfig.h"

#include <opencv2/opencv.hpp>

#include <memory>

#ifdef BVP_WITH_MINDVISION
class MindVisionCamera;
#endif

class InputManager {
public:
    /**
     * @brief 构造图像输入管理器。
     * @param config 输入配置，决定使用 mock、image、video 或 mindvision_camera。
     */
    explicit InputManager(const InputConfig& config);

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
     * @brief 释放输入资源。
     *
     * 对 video 模式会释放 VideoCapture；
     * 对 camera/mindvision_camera 模式会释放工业相机句柄。
     */
    void release();

private:
    InputConfig config_;
    cv::VideoCapture cap_;
    cv::Mat image_;
#ifdef BVP_WITH_MINDVISION
    std::unique_ptr<MindVisionCamera> mindvision_camera_;
#endif
    bool image_sent_ = false;
};
