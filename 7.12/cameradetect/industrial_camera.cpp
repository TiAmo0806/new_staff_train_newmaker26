/**
 * industrial_camera.cpp —— 工业相机（MindVision）操作实现
 */

#include "industrial_camera.hpp"

#include <iostream>

bool initIndustrialCamera(int index, CameraCtx& ctx)
{
    // 1. SDK 全局初始化（整个进程只需调用一次）
    CameraSdkStatus status = CameraSdkInit(1);  // 1 = 中文
    if (status != CAMERA_STATUS_SUCCESS) {
        std::cerr << "[ERROR] CameraSdkInit 失败, status=" << status << std::endl;
        return false;
    }

    // 2. 枚举设备
    tSdkCameraDevInfo devList[4];
    int camCount = 4;
    status = CameraEnumerateDevice(devList, &camCount);
    if (status != CAMERA_STATUS_SUCCESS) {
        std::cerr << "[ERROR] CameraEnumerateDevice 失败, status=" << status << std::endl;
        return false;
    }
    std::cout << "[INFO] 检测到 " << camCount << " 台工业相机" << std::endl;

    if (camCount == 0) {
        std::cerr << "[ERROR] 未找到工业相机" << std::endl;
        return false;
    }
    if (index >= camCount) {
        std::cerr << "[ERROR] 相机索引 " << index << " 超出范围 (0~"
                  << camCount - 1 << ")" << std::endl;
        return false;
    }

    // 3. 打印设备名
    std::cout << "[INFO] 相机[" << index << "]: "
              << devList[index].acProductName << " "
              << devList[index].acFriendlyName << std::endl;

    // 4. 初始化指定相机
    status = CameraInit(&devList[index], -1, -1, &ctx.hCamera);
    if (status != CAMERA_STATUS_SUCCESS) {
        std::cerr << "[ERROR] CameraInit 失败, status=" << status << std::endl;
        return false;
    }

    // 5. 获取相机能力
    tSdkCameraCapbility cap;
    status = CameraGetCapability(ctx.hCamera, &cap);
    if (status != CAMERA_STATUS_SUCCESS) {
        std::cerr << "[ERROR] CameraGetCapability 失败, status=" << status << std::endl;
        CameraUnInit(ctx.hCamera);
        return false;
    }

    // 6. 设置分辨率（使用默认最大分辨率）
    CameraSetImageResolution(ctx.hCamera, &cap.pImageSizeDesc[0]);
    ctx.width  = cap.pImageSizeDesc[0].iWidth;
    ctx.height = cap.pImageSizeDesc[0].iHeight;

    // 7. 分配 RGB 缓冲区
    ctx.channel = cap.sIspCapacity.bMonoSensor ? 1 : 3;
    ctx.rgbBuffer = (unsigned char*)malloc(
        cap.sResolutionRange.iHeightMax *
        cap.sResolutionRange.iWidthMax * 3);

    if (!ctx.rgbBuffer) {
        std::cerr << "[ERROR] 分配 RGB 缓冲区失败" << std::endl;
        CameraUnInit(ctx.hCamera);
        return false;
    }

    // 8. 设置 ISP 输出格式
    if (cap.sIspCapacity.bMonoSensor) {
        CameraSetIspOutFormat(ctx.hCamera, CAMERA_MEDIA_TYPE_MONO8);
    } else {
        CameraSetIspOutFormat(ctx.hCamera, CAMERA_MEDIA_TYPE_BGR8);
    }

    // 9. 开始取流
    status = CameraPlay(ctx.hCamera);
    if (status != CAMERA_STATUS_SUCCESS) {
        std::cerr << "[ERROR] CameraPlay 失败, status=" << status << std::endl;
        free(ctx.rgbBuffer);
        CameraUnInit(ctx.hCamera);
        return false;
    }

    std::cout << "[INFO] 工业相机已启动, 分辨率: " << ctx.width
              << "x" << ctx.height << ", 通道: " << ctx.channel << std::endl;
    return true;
}

bool captureIndustrialFrame(CameraCtx& ctx, cv::Mat& frame)
{
    tSdkFrameHead frameInfo;
    BYTE*         pbyBuffer = nullptr;

    CameraSdkStatus status = CameraGetImageBuffer(
        ctx.hCamera, &frameInfo, &pbyBuffer, ctx.frameTimeoutMs);

    if (status != CAMERA_STATUS_SUCCESS) {
        std::cerr << "[WARN] CameraGetImageBuffer 超时或失败, status="
                  << status << std::endl;
        return false;
    }

    // ISP 处理后数据写入 rgbBuffer
    CameraImageProcess(ctx.hCamera, pbyBuffer, ctx.rgbBuffer, &frameInfo);

    // 构造 cv::Mat（必须 clone，因为 Release 后 buffer 即失效）
    int cvType = (frameInfo.uiMediaType == CAMERA_MEDIA_TYPE_MONO8)
                     ? CV_8UC1 : CV_8UC3;
    cv::Mat tmp(frameInfo.iHeight, frameInfo.iWidth, cvType, ctx.rgbBuffer);
    frame = tmp.clone();

    // 释放 SDK 缓冲区
    CameraReleaseImageBuffer(ctx.hCamera, pbyBuffer);

    return !frame.empty();
}

void releaseIndustrialCamera(CameraCtx& ctx)
{
    if (ctx.hCamera >= 0) {
        CameraUnInit(ctx.hCamera);
        ctx.hCamera = -1;
    }
    if (ctx.rgbBuffer) {
        free(ctx.rgbBuffer);
        ctx.rgbBuffer = nullptr;
    }
    std::cout << "[INFO] 工业相机已释放" << std::endl;
}
