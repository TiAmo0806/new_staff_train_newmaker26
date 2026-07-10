/**
 * industrial_camera.hpp —— 工业相机（MindVision）操作
 *
 * 封装 MindVision 工业相机的初始化、帧捕获与资源释放的声明。
 * 函数实现位于 industrial_camera.cpp。
 */

#ifndef CAMERADETECT_INDUSTRIAL_CAMERA_HPP_
#define CAMERADETECT_INDUSTRIAL_CAMERA_HPP_

#include <opencv2/opencv.hpp>

#include "CameraApi.h"

// ============================================================
//  工业相机上下文
// ============================================================
struct CameraCtx {
    CameraHandle    hCamera   = -1;
    unsigned char*  rgbBuffer = nullptr;
    int             width     = 0;
    int             height    = 0;
    int             channel   = 3;  // BGR8 = 3 通道
};

// ============================================================
//  初始化工业相机
//  index: 相机枚举序号（0, 1, ...）
// ============================================================
bool initIndustrialCamera(int index, CameraCtx& ctx);

// ============================================================
//  从工业相机捕获一帧
//  返回 true 表示成功，frame 被填充为 BGR 格式的 cv::Mat
// ============================================================
bool captureIndustrialFrame(CameraCtx& ctx, cv::Mat& frame);

// ============================================================
//  释放工业相机资源
// ============================================================
void releaseIndustrialCamera(CameraCtx& ctx);

#endif  // CAMERADETECT_INDUSTRIAL_CAMERA_HPP_
