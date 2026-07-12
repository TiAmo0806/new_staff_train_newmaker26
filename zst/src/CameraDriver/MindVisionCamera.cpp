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
    // 初始化 MindVision SDK
    CameraSdkInit(1);

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
    const int maxW = capability_.sResolutionRange.iWidthMax;
    const int maxH = capability_.sResolutionRange.iHeightMax;

    bgrBuffer_.resize(static_cast<size_t>(maxW) * static_cast<size_t>(maxH) * 3);

    std::cout << "[Camera] 相机打开成功，最大分辨率: "
              << maxW << "x" << maxH << std::endl;

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
    if (handle_ < 0)
    {
        return false;
    }

    tSdkFrameHead head;
    BYTE *raw = nullptr;

    // 从相机获取一帧原始图像
    if (CameraGetImageBuffer(handle_, &head, &raw, 1000) != CAMERA_STATUS_SUCCESS)
    {
        return false;
    }

    // 将相机原始图像转换成 BGR8
    const int processStatus = CameraImageProcess(
        handle_,
        raw,
        bgrBuffer_.data(),
        &head
    );

    // 原始 buffer 用完必须释放，否则可能卡帧
    CameraReleaseImageBuffer(handle_, raw);

    if (processStatus != CAMERA_STATUS_SUCCESS)
    {
        return false;
    }

    // 用 OpenCV Mat 包装 BGR 缓存
    cv::Mat view(
        head.iHeight,
        head.iWidth,
        CV_8UC3,
        bgrBuffer_.data()
    );

    // 必须 clone，因为 bgrBuffer_ 下一帧会被覆盖
    image = view.clone();

    return true;
}

void MindVisionCamera::setExposureGain(int exposureUs, int gain)
{
    if (handle_ < 0)
    {
        return;
    }

    CameraSetExposureTime(handle_, exposureUs);
    CameraSetAnalogGain(handle_, gain);
}
