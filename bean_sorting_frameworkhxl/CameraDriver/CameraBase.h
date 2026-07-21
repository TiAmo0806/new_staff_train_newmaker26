#ifndef CAMERA_BASE_H_
  #define CAMERA_BASE_H_

  #include <opencv2/opencv.hpp>
  #include <string>

  class CameraBase {
  public:
      virtual ~CameraBase() = default;

      /// @brief 打开相机, 加载配置
      /// @param config_path  配置文件路径 (YAML)
      /// @return 成功返回 true
      virtual bool open(const std::string& config_path) = 0;

      /// @brief 关闭相机
      virtual void close() = 0;

      /// @brief 相机是否已打开
      virtual bool isOpened() const = 0;

      /// @brief 抓取一帧 BGR 图像
      /// @param[out] image  输出图像
      /// @return 成功返回 true
      virtual bool grab(cv::Mat& image) = 0;

      /// @brief 设置曝光时间 (us) 和模拟增益
      virtual bool setExposureGain(int exposure_us, int gain) = 0;

      /// @brief 获取图像宽度
      virtual int width()  const = 0;

      /// @brief 获取图像高度
      virtual int height() const = 0;
  };

  #endif  // CAMERA_BASE_H_