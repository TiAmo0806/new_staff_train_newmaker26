#include "CameraDriver/MindVisionCamera.h"

#include <chrono>
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
    // 正常0/1开关只暂停和恢复采集，不反复枚举、初始化、销毁USB相机。
    if (handle_ >= 0)
    {
        if (playing_) return true;
        const int status = CameraPlay(handle_);
        if (status != CAMERA_STATUS_SUCCESS)
        {
            std::cerr << "[Camera] 恢复采集失败，status=" << status << std::endl;
            // 暂停期间USB设备可能已经掉线。释放旧句柄后，主循环下一次重试会
            // 重新枚举设备，而不是永远对同一个失效句柄重复CameraPlay。
            close();
            return false;
        }
        acquisitionStarted_ = true;
        playing_ = true;
        std::cout << "[Camera] 已恢复采集（复用现有相机句柄）" << std::endl;
        return true;
    }

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
    acquisitionStarted_ = true;
    playing_ = true;

    // 根据相机最大分辨率分配 BGR 缓存
    const int maxW = capability_.sResolutionRange.iWidthMax;   // 最大宽度
    const int maxH = capability_.sResolutionRange.iHeightMax;  // 最大高度

    bgrBuffer_.resize(static_cast<size_t>(maxW) * static_cast<size_t>(maxH) * 3); // 3 通道 BGR

    std::cout << "[Camera] 相机打开成功，最大分辨率: "
              << maxW << "x" << maxH << std::endl;

    return true;
}

bool MindVisionCamera::pause()
{
    if (handle_ < 0 || !playing_) return true;   // 未初始化或已经暂停，视为目标状态已满足

    const int status = CameraPause(handle_);
    if (status != CAMERA_STATUS_SUCCESS)
    {
        std::cerr << "[Camera] 暂停采集失败，status=" << status << std::endl;
        return false;
    }

    playing_ = false;
    std::cout << "[Camera] 已暂停采集并保留相机句柄" << std::endl;
    return true;
}

void MindVisionCamera::close()
{
    if (handle_ >= 0)
    {
        const auto startedAt = std::chrono::steady_clock::now();

        // SDK文档说明CameraStop通常应在反初始化时调用。原代码在CameraPlay状态下
        // 直接CameraUnInit，部分Linux/USB环境会等待采集线程退出，从而卡住主循环。
        if (acquisitionStarted_)
        {
            const int stopStatus = CameraStop(handle_);
            if (stopStatus != CAMERA_STATUS_SUCCESS)
                std::cerr << "[Camera] 停止采集返回异常，status=" << stopStatus
                          << "，仍继续释放句柄" << std::endl;
        }
        acquisitionStarted_ = false;
        playing_ = false;

        const int uninitStatus = CameraUnInit(handle_);    // 停采后再释放MindVision资源
        handle_ = -1;                                      // 无论SDK返回值如何都不再复用旧句柄
        bgrBuffer_.clear();                                // 下次完整打开时按能力重新分配
        capability_ = {};

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startedAt).count();
        if (uninitStatus != CAMERA_STATUS_SUCCESS)
            std::cerr << "[Camera] 释放相机句柄返回异常，status=" << uninitStatus
                      << "，耗时=" << elapsedMs << "ms" << std::endl;
        else
            std::cout << "[Camera] 已停止并释放相机句柄，耗时="
                      << elapsedMs << "ms" << std::endl;
    }
}

bool MindVisionCamera::read(cv::Mat &image)
{
    if (handle_ < 0 || !playing_)
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
