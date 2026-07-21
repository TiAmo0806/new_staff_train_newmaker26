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
    iStatus = CameraEnumerateDevice(&tCameraEnumList, &iCameraCounts);
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
    CameraGetCapability(handle_, &cap_);

    max_width_  = cap_.sResolutionRange.iWidthMax;
    max_height_ = cap_.sResolutionRange.iHeightMax;
    mono_       = cap_.sIspCapacity.bMonoSensor;

    // 预分配 RGB 缓存，仅一次
    g_pRgbBuffer_ = (unsigned char*)malloc(
        cap_.sResolutionRange.iHeightMax *
        cap_.sResolutionRange.iWidthMax * 3);

    // 开始采集
    CameraPlay(handle_);

    if (cap_.sIspCapacity.bMonoSensor) {
        channel_ = 1;
        CameraSetIspOutFormat(handle_, CAMERA_MEDIA_TYPE_MONO8);
        printf("[MvCamera] 输出格式: MONO8\n");
    } else {
        channel_ = 3;
        CameraSetIspOutFormat(handle_, CAMERA_MEDIA_TYPE_BGR8);
        printf("[MvCamera] 输出格式: BGR8\n");
    }

    opened_   = true;
    grabbing_ = true;
    printf("[MvCamera] 相机就绪: %dx%d  timeout=%dms\n",
           max_width_, max_height_, grab_timeout_ms_);
    return true;
}

void MvCamera::close() {
    if (grabbing_) { CameraStop(handle_); grabbing_ = false; }
    if (handle_ >= 0) { CameraUnInit(handle_); handle_ = -1; }
    if (g_pRgbBuffer_) { free(g_pRgbBuffer_); g_pRgbBuffer_ = nullptr; }
    opened_ = false;
}

bool MvCamera::grab(cv::Mat& image) {
    if (!opened_ || !grabbing_) return false;

    tSdkFrameHead sFrameInfo;
    BYTE* pbyBuffer = nullptr;

    // 短超时: 200ms 防卡顿 (旧版 1000ms 会让人感觉冻结)
    if (CameraGetImageBuffer(handle_, &sFrameInfo, &pbyBuffer, grab_timeout_ms_)
        != CAMERA_STATUS_SUCCESS)
        return false;

    // SDK 解码到预分配缓存
    CameraImageProcess(handle_, pbyBuffer, g_pRgbBuffer_, &sFrameInfo);

    // ---- 性能优化: 浅引用, 不 clone (省 ~3.75MB/帧) ----
    // 主循环同步: detect() 只读, drawResults() 会画框到 image,
    // 下一次 grab() 的 CameraImageProcess 会完整覆盖 g_pRgbBuffer_
    cv::Mat view(
        cv::Size(sFrameInfo.iWidth, sFrameInfo.iHeight),
        sFrameInfo.uiMediaType == CAMERA_MEDIA_TYPE_MONO8 ? CV_8UC1 : CV_8UC3,
        g_pRgbBuffer_
    );
    image = view;   // 浅引用, 共享 g_pRgbBuffer_ 内存

    CameraReleaseImageBuffer(handle_, pbyBuffer);
    return !image.empty();
}

bool MvCamera::setExposureGain(int exposure_us, int gain) {
    if (!opened_) return true;
    CameraSetExposureTime(handle_, (double)exposure_us);
    CameraSetGain(handle_, gain, gain, gain);
    return true;
}
