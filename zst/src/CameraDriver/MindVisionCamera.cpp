#include "/home/zst/linuxSDK/include/CameraApi.h"
#include "/home/zst/zst/include/CameraDriver/MindVisionCamera.h"
#include <cstring>
#include <iostream>
#include <stdexcept>

MindVisionCamera::MindVisionCamera(const CameraConfig &config) : config_(config) {}

MindVisionCamera::~MindVisionCamera()
{
    close();
}

bool MindVisionCamera::open()
{
    // 初始化 MindVision SDK 并枚举相机。
    // CameraSdkInit(1) 是 MindVision Linux SDK 的初始化入口。
    CameraSdkInit(1);

    int count = 16;
    tSdkCameraDevInfo list[16];
    std::memset(list, 0, sizeof(list));
    if (CameraEnumerateDevice(list, &count) != CAMERA_STATUS_SUCCESS || count <= 0)
    {
        std::cerr << "[Camera] 未找到 MindVision 相机" << std::endl;
        return false;
    }
    if (config_.index < 0 || config_.index >= count)
    {
        std::cerr << "[Camera] 相机序号越界" << std::endl;
        return false;
    }

    if (CameraInit(&list[config_.index], -1, -1, &handle_) != CAMERA_STATUS_SUCCESS)
        return false;

    // 输出 BGR8，方便直接交给 OpenCV 和 YOLO。
    // 如果不设置，SDK 可能输出 Bayer/Mono 等格式，后面处理会很麻烦。
    CameraGetCapability(handle_, &capability_);
    CameraSetIspOutFormat(handle_, CAMERA_MEDIA_TYPE_BGR8);
    CameraSetAeState(handle_, config_.autoExposure ? TRUE : FALSE);
    setExposureGain(config_.exposureUs, config_.gain);

    if (CameraPlay(handle_) != CAMERA_STATUS_SUCCESS)
        return false;

    int maxW = capability_.sResolutionRange.iWidthMax;
    int maxH = capability_.sResolutionRange.iHeightMax;
    bgrBuffer_.resize(static_cast<size_t>(maxW) * maxH * 3);
    std::cout << "[Camera] opened, max=" << maxW << "x" << maxH << std::endl;
    return true;
}

void MindVisionCamera::close()
{
    if (handle_ >= 0)
    {
        CameraUnInit(handle_);
        handle_ = -1;
    }
}

bool MindVisionCamera::read(cv::Mat &image)
{
    if (handle_ < 0) return false;

    // SDK 原始 buffer 会复用，所以最后必须 clone。
    // CameraGetImageBuffer 拿到原始图；
    // CameraImageProcess 做 ISP 转换；
    // CameraReleaseImageBuffer 必须调用，否则会卡帧。
    tSdkFrameHead head;
    BYTE *raw = nullptr;
    if (CameraGetImageBuffer(handle_, &head, &raw, 1000) != CAMERA_STATUS_SUCCESS)
        return false;
    CameraImageProcess(handle_, raw, bgrBuffer_.data(), &head);
    CameraReleaseImageBuffer(handle_, raw);
    cv::Mat view(head.iHeight, head.iWidth, CV_8UC3, bgrBuffer_.data());
    image = view.clone();
    return true;
}

void MindVisionCamera::setExposureGain(int exposureUs, int gain)
{
    if (handle_ < 0) return;
    CameraSetExposureTime(handle_, exposureUs);
    CameraSetAnalogGain(handle_, gain);
}
