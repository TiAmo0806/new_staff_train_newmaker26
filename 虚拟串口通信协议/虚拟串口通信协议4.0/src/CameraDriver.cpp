/**
 * @file CameraDriver.cpp
 * @brief 迈德威视 USB3.0 相机驱动实现
 */

#include "CameraDriver.hpp"

#include <cstring>
#include <iostream>

#include "CameraApi.h"
#include "CameraDefine.h"
#include "CameraStatus.h"

#include <opencv2/imgproc.hpp>

namespace bean_sorter
{

CameraDriver::CameraDriver()
  : handle_(-1)//句柄
  , width_(0)
  , height_(0)
{
}

CameraDriver::~CameraDriver()
{
  Close();
}

bool CameraDriver::Open(int cameraIndex)
{
  // 1. 初始化 SDK
  if (CameraSdkInit(1) != CAMERA_STATUS_SUCCESS) {
    std::cerr << "[相机] SDK 初始化失败" << std::endl;
    return false;
  }

  // 2. 枚举相机
  tSdkCameraDevInfo cameraList[32];
  int cameraCount = 32;
  if (CameraEnumerateDevice(cameraList, &cameraCount) != CAMERA_STATUS_SUCCESS) {
    std::cerr << "[相机] 枚举失败" << std::endl;
    return false;
  }

  if (cameraCount == 0) {
    std::cerr << "[相机] 未检测到相机" << std::endl;
    return false;
  }

  if (cameraIndex >= cameraCount) {//如果传入的相机索引超出实际相机数量，报错退出
    std::cerr << "[相机] 索引越界: " << cameraIndex << "/" << cameraCount << std::endl;
    return false;
  }

  // 3. 打开相机
  int status = CameraInit(&cameraList[cameraIndex], -1, -1, &handle_);
  if (status != CAMERA_STATUS_SUCCESS) {
    std::cerr << "[相机] 打开失败, 错误码: " << status << std::endl;
    handle_ = -1;
    return false;
  }

  cameraName_ = cameraList[cameraIndex].acFriendlyName;//保存相机名称

  // 4. 设置为连续采集模式
  CameraSetTriggerMode(handle_, 0);

  // 5. 开始采集
  if (CameraPlay(handle_) != CAMERA_STATUS_SUCCESS) {
    std::cerr << "[相机] 开始采集失败" << std::endl;
    CameraUnInit(handle_);
    handle_ = -1;
    return false;
  }

  // 6. 获取分辨率
  tSdkImageResolution res;
  if (CameraGetImageResolution(handle_, &res) == CAMERA_STATUS_SUCCESS) {
    width_ = res.iWidth;
    height_ = res.iHeight;
  }

  std::cout << "[相机] 已打开: " << cameraName_
            << " (" << width_ << "x" << height_ << ")"
            << std::endl;
  return true;
}

void CameraDriver::Close()
{
  if (handle_ >= 0) {
    CameraStop(handle_);
    CameraUnInit(handle_);
    handle_ = -1;
    std::cout << "[相机] 已关闭" << std::endl;
  }
}

cv::Mat CameraDriver::GrabFrame()
{
  if (handle_ < 0) {
    return cv::Mat();
  }

  tSdkFrameHead header;//获取图像缓冲区，做图像处理
  uint8_t * buffer = nullptr;

  if (CameraGetImageBuffer(handle_, &header, &buffer, 1000) != CAMERA_STATUS_SUCCESS) {
    return cv::Mat();
  }

  int w = header.iWidth;
  int h = header.iHeight;

  // SDK 输出 RGB, 转 OpenCV BGR
  cv::Mat frame(h, w, CV_8UC3);
  CameraImageProcess(handle_, buffer, frame.data, &header);
  cv::cvtColor(frame, frame, cv::COLOR_RGB2BGR);

  CameraReleaseImageBuffer(handle_, buffer);

  return frame;
}

bool CameraDriver::SetExposureTime(double us)//设置曝光时间
{
  if (handle_ < 0) return false;
  return CameraSetExposureTime(handle_, us) == CAMERA_STATUS_SUCCESS;
}

bool CameraDriver::SetGain(float gain)//设置增益，提高相机亮度，但会增加噪点
{
  if (handle_ < 0) return false;
  return CameraSetAnalogGain(handle_, gain) == CAMERA_STATUS_SUCCESS;
}

bool CameraDriver::SetTriggerMode(bool softTrigger)//设置触发模式
{
  if (handle_ < 0) return false;
  return CameraSetTriggerMode(handle_, softTrigger ? 1 : 0) == CAMERA_STATUS_SUCCESS;
}

}  // namespace bean_sorter