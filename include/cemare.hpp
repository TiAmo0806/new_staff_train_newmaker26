/**
 * camera.hpp —— 迈德威视工业相机封装
 * 基于 MindVision SDK (CameraApi.h)
 */

#ifndef CAMERA_HPP_
#define CAMERA_HPP_

#include <opencv2/opencv.hpp>
#include <iostream>
#include <chrono>
#include <optional>
#include <CameraApi.h>

class Camera
{
public:
    Camera();
    ~Camera();

    // 打开相机（自动枚举第一个可用相机）
    // exposure_time: 手动曝光时间(微秒)，-1=自动曝光
    // analog_gain:   模拟增益，-1=自动
    bool open(int width = 640, int height = 480,
              double exposureTime = -1.0, int analogGain = -1);

    // 获取一帧图像（阻塞，返回 BGR 格式 cv::Mat）
    cv::Mat getFrame();

    // 安全取帧：自动检测空帧并通过状态机重连（不阻塞主线程）
    // 返回 nullopt 表示跳过此帧（重连等待中或取帧失败）
    std::optional<cv::Mat> getFrameSafe(int emptyThreshold = 50,
                                        int reconnectDelayMs = 500);

    // 关闭相机
    void close();

    // 检查相机是否已打开
    bool isOpened() const;

    // 获取当前分辨率
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }

private:
    enum class CamState { NORMAL, WAITING };

    int hCamera_;                       // 相机句柄
    int width_;
    int height_;
    int channel_;                       // 1=黑白, 3=彩色
    bool opened_;
    unsigned char* rgb_buffer_;         // BGR 图像缓存
    int emptyCount_ = 0;                // 连续空帧计数
    double exposureTime_ = -1.0;        // 保存曝光时间用于重连
    int analogGain_ = -1;               // 保存模拟增益用于重连

    CamState camState_ = CamState::NORMAL;
    std::chrono::steady_clock::time_point waitStart_;
};

#endif  // CAMERA_HPP_