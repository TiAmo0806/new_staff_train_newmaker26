/**
 * @file MindVisionCamera.cpp
 * @brief 迈德威视（MindVision）工业相机驱动实现
 *
 * 封装了迈德威视 SDK 的基本操作：
 *   初始化（CameraSdkInit）
 *   设备枚举（CameraEnumerateDevice）
 *   打开相机（CameraInit）
 *   设置触发模式（连续采集）
 *   开始/停止采集（CameraPlay / CameraUnInit）
 *   读取帧（CameraGetImageBuffer → Bayer→BGR → ReleaseImageBuffer）
 *
 * @note Bayer 到 BGR 的转换模式：cv::COLOR_BayerRG2BGR
 *       此转换模式经过实际测试确认在比赛场景下表现最佳。
 *
 * @see include/MindVisionCamera.hpp
 */

#include "../include/MindVisionCamera.hpp"
#include <cstring>
#include <iostream>

MindVisionCamera::MindVisionCamera() {
    m_device_id = -1;
    m_is_opened = false;
    m_image_width = 0;
    m_image_height = 0;
}

MindVisionCamera::~MindVisionCamera() {
    close();
}

// ============================================================
//  open —— 枚举并打开迈德威视工业相机
//
//  完整流程:
//    1. CameraSdkInit(1) — 全局初始化（整个进程只需一次）
//    2. CameraEnumerateDevice — 查找连接的迈德威视相机
//    3. CameraInit — 选择第 camera_index 个相机并初始化
//    4. CameraSetTriggerMode(0) — 设置为连续采集模式
//    5. CameraPlay — 开始输出图像流
// ============================================================

bool MindVisionCamera::open(int camera_index) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_is_opened) {
        std::cout << "[Camera] Already opened" << std::endl;
        return true;
    }

    // ⭐ SDK 全局初始化（整个进程只需调用一次即可）
    //    参考 cameradetect/industrial_camera.cpp 的实现
    CameraSdkStatus initRet = CameraSdkInit(1);  // 1 = 中文语言
    if (initRet != CAMERA_STATUS_SUCCESS) {
        std::cerr << "[Camera] CameraSdkInit 失败! 错误码: " << initRet << std::endl;
        // 非致命错误：即使初始化返回错误，仍可继续尝试枚举
    }

    // ⭐ 零初始化设备信息结构体，支持多相机枚举
    tSdkCameraDevInfo tCameraInfo[8];
    memset(tCameraInfo, 0, sizeof(tCameraInfo));
    int iCameraCounts = 8;  // 最多搜索 8 个相机

    CameraSdkStatus ret = CameraEnumerateDevice(tCameraInfo, &iCameraCounts);
    if (ret != CAMERA_STATUS_SUCCESS || iCameraCounts <= 0) {
        std::cerr << "[Camera] 未找到迈德威视相机！错误码: " << ret << std::endl;
        return false;
    }

    std::cout << "[Camera] Found " << iCameraCounts << " device(s)" << std::endl;

    // 选择指定索引的相机
    if (camera_index >= iCameraCounts) {
        std::cerr << "[Camera] 相机索引 " << camera_index
                  << " 超出范围 (0~" << iCameraCounts - 1 << ")" << std::endl;
        return false;
    }

    ret = CameraInit(&tCameraInfo[camera_index], -1, -1, &m_device_id);
    if (ret != CAMERA_STATUS_SUCCESS) {
        std::cerr << "[Camera] 相机初始化失败！错误码: " << ret << std::endl;
        return false;
    }

    // 设置为连续采集模式（而非软触发或硬触发）
    CameraSetTriggerMode(m_device_id, 0);
    // 开始输出图像
    CameraPlay(m_device_id);

    // 读取初始分辨率
    tSdkImageResolution resolution;
    CameraGetImageResolution(m_device_id, &resolution);
    m_image_width = resolution.iWidth;
    m_image_height = resolution.iHeight;

    std::cout << "[Camera] Opened: " << m_image_width << "x" << m_image_height << std::endl;

    m_is_opened = true;
    std::cout << "[Camera] Started streaming" << std::endl;

    return true;
}

// ============================================================
//  close —— 关闭相机
// ============================================================

void MindVisionCamera::close() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_is_opened) return;

    CameraUnInit(m_device_id);

    m_is_opened = false;
    m_device_id = -1;

    std::cout << "[Camera] Closed" << std::endl;
}

// ============================================================
//  read —— 采集一帧图像
//
//  流程:
//    1. CameraGetImageBuffer 获取 Bayer 原始数据（阻塞等待 1000ms）
//    2. 将原始数据封装为 cv::Mat（CV_8UC1，单通道）
//    3. cv::cvtColor 使用 BayerRG→BGR 转换为彩色图
//    4. CameraReleaseImageBuffer 释放 SDK 内部缓存
//
//  ⚠ 注意：必须每次调用 CameraReleaseImageBuffer 释放缓存，
//    否则 SDK 内部缓冲区很快耗尽，CameraGetImageBuffer 会阻塞。
// ============================================================

bool MindVisionCamera::read(cv::Mat& frame) {
    if (!m_is_opened) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    tSdkFrameHead frameHead;
    BYTE* pFrameBuffer = NULL;

    CameraSdkStatus ret = CameraGetImageBuffer(m_device_id, &frameHead, &pFrameBuffer, 1000);
    if (ret != CAMERA_STATUS_SUCCESS) {
        std::cerr << "[Camera] CameraGetImageBuffer failed: " << ret << std::endl;
        return false;
    }

    if (pFrameBuffer == NULL) {
        std::cerr << "[Camera] pFrameBuffer is NULL!" << std::endl;
        return false;
    }

    m_image_width = frameHead.iWidth;
    m_image_height = frameHead.iHeight;

    // ⭐ Bayer RG → BGR 转换（经多次测试确认该模式表现最正常）
    cv::Mat raw(m_image_height, m_image_width, CV_8UC1, pFrameBuffer);
    cv::cvtColor(raw, frame, cv::COLOR_BayerRG2BGR);

    // ⭐ 必须释放缓存，否则 SDK 内部缓存会被耗尽
    CameraReleaseImageBuffer(m_device_id, pFrameBuffer);

    return !frame.empty();
}

// ============================================================
//  相机参数设置（简单封装 SDK 接口）
// ============================================================

void MindVisionCamera::setExposureTime(int exposure_us) {
    if (!m_is_opened) return;
    CameraSetExposureTime(m_device_id, exposure_us);
}

void MindVisionCamera::setGain(int gain) {
    if (!m_is_opened) return;
    CameraSetGain(m_device_id, gain, gain, gain);
}

void MindVisionCamera::setResolution(int width, int height) {
    if (!m_is_opened) return;
    // 设置相机分辨率（具体实现取决于 SDK 版本）
}
