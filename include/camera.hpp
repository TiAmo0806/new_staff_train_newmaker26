#ifndef CAMERA_HPP
#define CAMERA_HPP

#include "CameraApi.h"
#include <opencv2/opencv.hpp>

/**
 * 迈德威视工业相机封装
 *
 * 使用方式：
 *   Camera cam;
 *   if (!cam.open()) return -1;
 *   cv::Mat frame;
 *   while (cam.read(frame)) {
 *       // 处理 frame...
 *   }
 *   cam.release();
 */
class Camera {
public:
    Camera();
    ~Camera();

    /// 打开并初始化相机，成功返回 true
    bool open();

    /// 读取一帧图像（阻塞，超时 1000ms），成功返回 true
    bool read(cv::Mat& frame);

    /// 关闭相机释放资源
    void release();

    /// 获取当前帧宽度
    int width()  const { return m_width; }

    /// 获取当前帧高度
    int height() const { return m_height; }

    /// 是否为黑白传感器
    bool isMono() const { return m_channels == 1; }

private:
    int         m_hCamera;      // 相机句柄
    int         m_channels;     // 通道数（1=灰度, 3=BGR）
    int         m_width;        // 当前帧宽
    int         m_height;       // 当前帧高
    cv::Mat     m_rgbBuf;       // 预分配缓存（零拷贝）
    bool        m_opened;       // 是否已打开
};

#endif // CAMERA_HPP
