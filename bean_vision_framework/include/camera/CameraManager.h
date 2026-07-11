#pragma once

#include "core/AppConfig.h"

#include <opencv2/core.hpp>

#include <memory>

class CameraManager {
public:
    /**
     * @brief 构造工业相机管理器。
     * @param config 工业相机配置。
     */
    explicit CameraManager(const CameraConfig& config);

    /**
     * @brief 析构时释放工业相机资源。
     */
    ~CameraManager();

    /**
     * @brief 打开工业相机。
     * @return 打开成功返回 true。
     */
    bool open();

    /**
     * @brief 读取一帧 BGR 图像。
     * @param frame 输出图像。
     * @return 读取成功返回 true。
     */
    bool read(cv::Mat& frame);

    /**
     * @brief 关闭工业相机并释放资源。
     */
    void close();

    /**
     * @brief 判断当前是否已经成功打开工业相机。
     * @return 打开状态。
     */
    bool isOpened() const;

private:
    struct Impl;

    CameraConfig config_;
    std::unique_ptr<Impl> impl_;
    bool opened_ = false;
};
