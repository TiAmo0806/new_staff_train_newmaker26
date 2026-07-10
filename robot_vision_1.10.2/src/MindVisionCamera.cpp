#include "../include/MindVisionCamera.hpp"
#include <iostream>
#include <cstring>

MindVisionCamera::MindVisionCamera() {
    m_device_id = -1;
    m_is_opened = false;
    m_image_width = 0;
    m_image_height = 0;
}

MindVisionCamera::~MindVisionCamera() {
    close();
}

bool MindVisionCamera::open(int camera_index) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_is_opened) {
        std::cout << "[Camera] Already opened" << std::endl;
        return true;
    }
    
    tSdkCameraDevInfo tCameraInfo;
    int iCameraCounts = 1;
    
    CameraSdkStatus ret = CameraEnumerateDevice(&tCameraInfo, &iCameraCounts);
    if (ret != CAMERA_STATUS_SUCCESS || iCameraCounts <= 0) {
        std::cerr << "[Camera] 未找到迈德威视相机！" << std::endl;
        return false;
    }
    
    std::cout << "[Camera] Found " << iCameraCounts << " device(s)" << std::endl;
    
    ret = CameraInit(&tCameraInfo, -1, -1, &m_device_id);
    if (ret != CAMERA_STATUS_SUCCESS) {
        std::cerr << "[Camera] 相机初始化失败！" << std::endl;
        return false;
    }
    
    CameraSetTriggerMode(m_device_id, 0);
    CameraPlay(m_device_id);
    
    tSdkImageResolution resolution;
    CameraGetImageResolution(m_device_id, &resolution);
    m_image_width = resolution.iWidth;
    m_image_height = resolution.iHeight;
    
    std::cout << "[Camera] Opened: " << m_image_width << "x" << m_image_height << std::endl;
    
    m_is_opened = true;
    std::cout << "[Camera] Started streaming" << std::endl;
    
    return true;
}

void MindVisionCamera::close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_is_opened) return;
    
    CameraUnInit(m_device_id);
    
    m_is_opened = false;
    m_device_id = -1;
    
    std::cout << "[Camera] Closed" << std::endl;
}

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
    
    // ⭐ Bayer RG → BGR 转换（测试确认最正常）
    cv::Mat raw(m_image_height, m_image_width, CV_8UC1, pFrameBuffer);
    cv::cvtColor(raw, frame, cv::COLOR_BayerRG2BGR);
    
    CameraReleaseImageBuffer(m_device_id, pFrameBuffer);
    
    return !frame.empty();
}

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
}