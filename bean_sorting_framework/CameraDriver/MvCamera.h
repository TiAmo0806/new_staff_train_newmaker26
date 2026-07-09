#ifndef MV_CAMERA_H_
  #define MV_CAMERA_H_

  #include "CameraBase.h"
  #include <CameraApi.h>
  #include <string>
  #include <opencv2/opencv.hpp>

  class MvCamera : public CameraBase {
  public:
      MvCamera();
      ~MvCamera() override;

      MvCamera(const MvCamera&) = delete;
      MvCamera& operator=(const MvCamera&) = delete;

      bool open(const std::string& config_path) override;
      void close() override;
      bool isOpened() const override { return opened_; }

      bool grab(cv::Mat& image) override;
      bool setExposureGain(int exposure_us, int gain) override;

      int width()  const override { return max_width_; }
      int height() const override { return max_height_; }

  private:
      int handle_ = -1;
      int max_width_  = 1280;
      int max_height_ = 1024;
      int channel_ = 3;
      bool opened_   = false;
      bool grabbing_ = false;
      bool mono_     = false;

      tSdkCameraCapbility cap_;
      unsigned char* g_pRgbBuffer_ = nullptr;
  };

  #endif