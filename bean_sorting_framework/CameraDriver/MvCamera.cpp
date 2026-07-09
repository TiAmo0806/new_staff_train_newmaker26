#include "MvCamera.h"
  #include <iostream>

  MvCamera::MvCamera() {}

  MvCamera::~MvCamera() { close(); }

  bool MvCamera::open(const std::string& config_path) {
      if (opened_) return true;
      (void)config_path;

      int iStatus;

      // SDK初始化
      CameraSdkInit(1);

      // 枚举设备 — count 必须初始化为 1
      tSdkCameraDevInfo tCameraEnumList;
      int iCameraCounts = 1;
      iStatus = CameraEnumerateDevice(&tCameraEnumList, &iCameraCounts);//枚举电脑上的相机
      printf("[MvCamera] EnumDevice state=%d count=%d\n", iStatus, iCameraCounts);
      if (iCameraCounts == 0) {
          std::cerr << "[MvCamera] 未检测到相机" << std::endl;
          return false;
      }

      // 相机初始化
      iStatus = CameraInit(&tCameraEnumList, -1, -1, &handle_);
      printf("[MvCamera] Init state=%d\n", iStatus);
      if (iStatus != CAMERA_STATUS_SUCCESS) {
          std::cerr << "[MvCamera] CameraInit 失败" << std::endl;
          return false;
      }

      // 获得相机的特性描述结构体
      CameraGetCapability(handle_, &cap_);//获取相机参数

      max_width_  = cap_.sResolutionRange.iWidthMax;
      max_height_ = cap_.sResolutionRange.iHeightMax;
      mono_       = cap_.sIspCapacity.bMonoSensor;

      // 分配 RGB 缓存（ demo: malloc）
      g_pRgbBuffer_ = (unsigned char*)malloc(
          cap_.sResolutionRange.iHeightMax *
          cap_.sResolutionRange.iWidthMax * 3);

      // 开始采集（先 Play，后 SetIspOutFormat，同 demo）
      CameraPlay(handle_);

      if (cap_.sIspCapacity.bMonoSensor) {
          channel_ = 1;
          CameraSetIspOutFormat(handle_, CAMERA_MEDIA_TYPE_MONO8);//设置输出格式（彩色BGR / 黑白MONO）
          printf("[MvCamera] 输出格式: MONO8\n");
      } else {
          channel_ = 3;
          CameraSetIspOutFormat(handle_, CAMERA_MEDIA_TYPE_BGR8);
          printf("[MvCamera] 输出格式: BGR8（与 OpenCV 一致）\n");
      }

      opened_   = true;
      grabbing_ = true;
      printf("[MvCamera] 相机就绪: %dx%d\n", max_width_, max_height_);
      return true;
  }

  void MvCamera::close() {
      if (grabbing_) { CameraStop(handle_); grabbing_ = false; }//停止采集
      if (handle_ >= 0) { CameraUnInit(handle_); handle_ = -1; }//反初始化相机
      if (g_pRgbBuffer_) { free(g_pRgbBuffer_); g_pRgbBuffer_ = nullptr; }
      opened_ = false;
  }

  bool MvCamera::grab(cv::Mat& image) {
      if (!opened_ || !grabbing_) return false;

      tSdkFrameHead sFrameInfo;
      BYTE* pbyBuffer = nullptr;

      if (CameraGetImageBuffer(handle_, &sFrameInfo, &pbyBuffer, 1000) != CAMERA_STATUS_SUCCESS)
          return false;//CameraGetImageBuffer从相机获取原始图像数据

      // 将原始图像数据转换为RGB/MONO格式
      CameraImageProcess(handle_, pbyBuffer, g_pRgbBuffer_, &sFrameInfo);

      // 照搬官方 demo: 完全一致的 Mat 构造，不 clone，不 cvtColor
      cv::Mat matImage(
          cv::Size(sFrameInfo.iWidth, sFrameInfo.iHeight),
          sFrameInfo.uiMediaType == CAMERA_MEDIA_TYPE_MONO8 ? CV_8UC1 : CV_8UC3,
          g_pRgbBuffer_
      );

      // 拷贝一份出去（否则下一帧会覆盖共享内存）
      image = matImage.clone();

      CameraReleaseImageBuffer(handle_, pbyBuffer);//释放图像缓冲区
      return !image.empty();
  }

  bool MvCamera::setExposureGain(int exposure_us, int gain) {
      if (!opened_) return true;
      CameraSetExposureTime(handle_, (double)exposure_us);//设置曝光时间
      CameraSetGain(handle_, gain, gain, gain);//设置增益
      return true;
  }