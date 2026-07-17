#include "CameraDriver/MindVisionCamera.h"

#include <iostream>

MindVisionCamera::MindVisionCamera(const CameraConfig &config)
    : config_(config)
{
}

MindVisionCamera::~MindVisionCamera()
{
    close();
}

bool MindVisionCamera::open()
{
    if (handle_ >= 0) return true;               // 已打开时避免重复初始化设备

    // SDK全局初始化只做一次；相机掉线重连时只重新枚举并CameraInit设备。
    if (!sdkInitialized_)
    {
        CameraSdkInit(1);
        sdkInitialized_ = true;
    }

    int count = 1;
    tSdkCameraDevInfo cameraInfo[1] = {};

    // 查找相机
    if (CameraEnumerateDevice(cameraInfo, &count) != CAMERA_STATUS_SUCCESS || count <= 0)
    {
        std::cerr << "[Camera] 未找到 MindVision 相机" << std::endl;
        return false;
    }

    // 打开找到的第一个相机
    if (CameraInit(&cameraInfo[0], -1, -1, &handle_) != CAMERA_STATUS_SUCCESS)
    {
        std::cerr << "[Camera] 相机初始化失败" << std::endl;
        return false;
    }

    // 获取相机能力参数
    if (CameraGetCapability(handle_, &capability_) != CAMERA_STATUS_SUCCESS)
    {
        std::cerr << "[Camera] 获取相机能力参数失败" << std::endl;
        close();
        return false;
    }

    // 设置输出格式为 BGR8，方便 OpenCV 和 YOLO 直接使用
    if (CameraSetIspOutFormat(handle_, CAMERA_MEDIA_TYPE_BGR8) != CAMERA_STATUS_SUCCESS)
    {
        std::cerr << "[Camera] 设置 BGR8 输出失败" << std::endl;
        close();
        return false;
    }

    // 设置自动曝光开关
    if (CameraSetAeState(handle_, config_.autoExposure ? TRUE : FALSE) != CAMERA_STATUS_SUCCESS)
    {
        std::cerr << "[Camera] 设置自动曝光状态失败" << std::endl;
        close();
        return false;
    }

    // 如果关闭自动曝光，就使用手动曝光和增益
    if (!config_.autoExposure)
    {
        setExposureGain(config_.exposureUs, config_.gain);
    }

    // 启动相机采集
    if (CameraPlay(handle_) != CAMERA_STATUS_SUCCESS)
    {
        std::cerr << "[Camera] 相机启动失败" << std::endl;
        close();
        return false;
    }

    // 根据相机最大分辨率分配 BGR 缓存
    const int maxW = capability_.sResolutionRange.iWidthMax;   // 最大宽度
    const int maxH = capability_.sResolutionRange.iHeightMax;  // 最大高度

    bgrBuffer_.resize(static_cast<size_t>(maxW) * static_cast<size_t>(maxH) * 3); // 3 通道 BGR

    std::cout << "[Camera] 相机打开成功，最大分辨率: "
              << maxW << "x" << maxH << std::endl;

    return true;
}

void MindVisionCamera::close()
{
    if (handle_ >= 0)
    {
        CameraUnInit(handle_);                  // 释放 MindVision 相机资源
        handle_ = -1;                           // 标记句柄为未打开
    }
}

bool MindVisionCamera::read(cv::Mat &image)
{
    if (handle_ < 0)
    {
        return false;                           // 相机未打开
    }

    tSdkFrameHead head;
    BYTE *raw = nullptr;

    // 从相机获取一帧原始图像
    if (CameraGetImageBuffer(handle_, &head, &raw, config_.frameTimeoutMs) != CAMERA_STATUS_SUCCESS)
    {
        return false;                           // 取图超时或失败
    }

    // 将相机原始图像转换成 BGR8
    const int processStatus = CameraImageProcess(
        handle_,
        raw,                                    // 输入的原始图像数据
        bgrBuffer_.data(),                      // 输出的 BGR8 缓存
        &head                                   // 帧头信息（宽、高、格式等）
    );

    // 原始 buffer 用完必须释放，否则可能卡帧
    CameraReleaseImageBuffer(handle_, raw);

    if (processStatus != CAMERA_STATUS_SUCCESS)
    {
        return false;                           // 图像处理失败
    }

    // 用 OpenCV Mat 包装 BGR 缓存
    cv::Mat view(
        head.iHeight,                           // 图像高度
        head.iWidth,                            // 图像宽度
        CV_8UC3,                                // 3 通道 8 位无符号整数
        bgrBuffer_.data()                       // 像素数据指针
    );

    // main会在下一次read之前完成YOLO推理和调试图clone，因此这里可以直接引用复用缓存，
    // 少做一次整幅图像深拷贝。调用方不能跨越下一次read长期保存这个Mat。
    image = view;

    return true;
}

void MindVisionCamera::setExposureGain(int exposureUs, int gain)
{
    if (handle_ < 0)
    {
        return;                                 // 相机未打开，无法设置
    }

    CameraSetExposureTime(handle_, exposureUs); // 设置曝光时间（微秒）
    CameraSetAnalogGain(handle_, gain);          // 设置模拟增益
}
