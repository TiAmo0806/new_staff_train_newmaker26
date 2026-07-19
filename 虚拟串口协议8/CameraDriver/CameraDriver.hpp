/**
 * @file CameraDriver.hpp
 * @brief 迈德威视 USB3.0 相机驱动 (MVSDK)
 */

#ifndef BEAN_SORTER__CAMERA_DRIVER_HPP_
#define BEAN_SORTER__CAMERA_DRIVER_HPP_

#include <string>
#include <opencv2/core.hpp>

namespace bean_sorter
{

class CameraDriver
{
public:
  CameraDriver();
  ~CameraDriver();
  CameraDriver(const CameraDriver &) = delete;//拷贝构造函数，禁止复制更安全，防止把相机对象拷贝导致资源混乱
  CameraDriver & operator=(const CameraDriver &) = delete;//禁止拷贝赋值

  /** 打开相机 (枚举第一个可用设备) */
  bool Open(int cameraIndex = 0);

  /** 关闭相机 */
  void Close();

  /** 是否已打开 */
  bool IsOpen() const { return handle_ >= 0; }

  /** 拍一帧, 返回 OpenCV Mat (BGR格式) */
  cv::Mat GrabFrame();

  /** 获取相机名称 */
  std::string GetCameraName() const { return cameraName_; }

  /** 获取图像宽度 */
  int GetWidth() const { return width_; }

  /** 获取图像高度 */
  int GetHeight() const { return height_; }

private:
  int handle_;          // 相机句柄 (CameraHandle)
  std::string cameraName_;
  int width_;
  int height_;

  cv::Mat frameBuffer_;  // 预分配帧缓存，避免每帧重新分配
  int bufferW_ = 0;
  int bufferH_ = 0;
};

}  // namespace bean_sorter

#endif  // BEAN_SORTER__CAMERA_DRIVER_HPP_
