/**
 * @file MindVisionCamera.hpp
 * @brief 迈德威视（MindVision）工业相机驱动封装
 *
 * 封装迈德威视 SDK 的相机枚举、初始化、图像采集和 Bayer→BGR 转换。
 * 该相机用于比赛场景中的实时取豆区/放置区图像采集。
 *
 * 使用方式:
 * @code
 *   MindVisionCamera camera;
 *   camera.open(0);          // 打开索引 0 的相机
 *   camera.read(frame);      // 读取一帧
 *   camera.setExposureTime(10000);  // 设置曝光为 10000 μs
 * @endcode
 *
 * @note 依赖迈德威视 SDK 头文件: CameraApi.h, CameraDefine.h, CameraStatus.h
 *       SDK 路径通过 CMakeLists.txt 自动检测或由 MINDVISION_SDK 环境变量指定。
 */

#ifndef MINDVISION_CAMERA_HPP
#define MINDVISION_CAMERA_HPP

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <mutex>

#include "CameraApi.h"
#include "CameraDefine.h"
#include "CameraStatus.h"

/**
 * @brief 迈德威视工业相机封装
 *
 * 提供线程安全的打开/关闭/读取操作。
 * 图像读取使用内部 mutex 保护，支持多线程场景。
 */
class MindVisionCamera {
public:
    MindVisionCamera();
    ~MindVisionCamera();

    /**
     * @brief 打开相机并开始输出图像流
     * @param camera_index 设备索引（0 表示第一个相机，-1 自动选择）
     * @return true 成功打开并开始输出
     *
     * 内部流程:
     *   CameraSdkInit → CameraEnumerateDevice → CameraInit
     *   → CameraSetTriggerMode(0, 连续采集) → CameraPlay
     */
    bool open(int camera_index = 0);
    void close();
    bool isOpened() const { return m_is_opened; }

    /**
     * @brief 采集一帧图像
     * @param frame [out] 输出 BGR 格式的 cv::Mat
     * @return true 成功读取
     *
     * 内部调用 CameraGetImageBuffer 获取 Bayer 原始数据，
     * 使用 cv::COLOR_BayerRG2BGR 转换为 BGR 格式。
     * 读取完成后调用 CameraReleaseImageBuffer 释放缓存。
     */
    bool read(cv::Mat& frame);

    void setExposureTime(int exposure_us);
    void setGain(int gain);
    void setResolution(int width, int height);

private:
    int m_device_id = -1;      // SDK 分配的设备句柄
    bool m_is_opened = false;  // 相机是否已打开
    int m_image_width = 0;     // 当前图像宽度
    int m_image_height = 0;    // 当前图像高度
    std::mutex m_mutex;        // 线程安全锁
};

#endif