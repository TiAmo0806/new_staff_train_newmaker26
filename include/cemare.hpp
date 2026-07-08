/**
 * camera.hpp —— 迈德威视工业相机封装
 * 基于 MindVision SDK (CameraApi.h)
 */

#ifndef CAMERA_HPP_
#define CAMERA_HPP_

#include <opencv2/opencv.hpp>
#include <iostream>
#include <CameraApi.h>

class Camera
{
public:
    Camera();
    ~Camera();

    // 打开相机（自动枚举第一个可用相机）
    bool open(int width = 640, int height = 480);
    
    // 获取一帧图像（阻塞，返回 BGR 格式 cv::Mat）
    cv::Mat getFrame();

    // 关闭相机
    void close();

    // 检查相机是否已打开
    bool isOpened() const;

    // 获取当前分辨率
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }

    // 开始/停止录像（录像保存在当前目录）
    void startRecording(const std::string& filename = "record.avi", int fps = 25);
    void stopRecording();
    bool isRecording() const { return is_recording_; }

private:
    int hCamera_;                       // 相机句柄
    int width_;
    int height_;
    int channel_;                       // 1=黑白, 3=彩色
    bool opened_;
    unsigned char* rgb_buffer_;         // BGR 图像缓存

    // 录像相关
    cv::VideoWriter writer_;
    bool is_recording_;
    cv::Size frame_size_;
};

#endif  // CAMERA_HPP_