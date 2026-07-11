#include "/home/newmaker-11/mvs_openvino_demo/include/camera.hpp"
#include <stdio.h>

Camera::Camera()
    : m_hCamera(-1)
    , m_channels(3)
    , m_width(0)
    , m_height(0)
    , m_opened(false)
{}

Camera::~Camera() {
    release();
}

bool Camera::open() {
    printf("[Camera] 初始化相机...\n");

    // 1. SDK 全局初始化
    CameraSdkInit(1);

    // 2. 枚举相机设备
    int cameraCount = 1;
    tSdkCameraDevInfo enumList;
    int status = CameraEnumerateDevice(&enumList, &cameraCount);
    printf("[Camera] 枚举状态=%d, 数量=%d\n", status, cameraCount);
    if (cameraCount == 0) {
        fprintf(stderr, "[Camera] ERROR: 未检测到相机\n");
        return false;
    }

    // 3. 初始化相机（-1 表示默认通道、默认分辨率）
    status = CameraInit(&enumList, -1, -1, &m_hCamera);
    if (status != CAMERA_STATUS_SUCCESS) {
        fprintf(stderr, "[Camera] ERROR: 初始化失败, status=%d\n", status);
        return false;
    }

    // 4. 读取相机能力
    tSdkCameraCapbility cap;
    CameraGetCapability(m_hCamera, &cap);
    int maxW = cap.sResolutionRange.iWidthMax;
    int maxH = cap.sResolutionRange.iHeightMax;
    printf("[Camera] 最大分辨率: %d x %d\n", maxW, maxH);

    // 5. 预分配图像缓存（Mat RAII 管理内存）
    m_rgbBuf = cv::Mat(maxH, maxW, CV_8UC3);

    // 6. 根据传感器类型设置输出格式
    if (cap.sIspCapacity.bMonoSensor) {
        m_channels = 1;
        CameraSetIspOutFormat(m_hCamera, CAMERA_MEDIA_TYPE_MONO8);
        printf("[Camera] 传感器: 黑白\n");
    } else {
        m_channels = 3;
        CameraSetIspOutFormat(m_hCamera, CAMERA_MEDIA_TYPE_BGR8);
        printf("[Camera] 传感器: 彩色\n");
    }

    // 7. 启动数据流
    CameraPlay(m_hCamera);
    m_opened = true;
    printf("[Camera] 初始化完成\n");
    return true;
}

bool Camera::read(cv::Mat& frame) {
    if (!m_opened) return false;

    tSdkFrameHead frameInfo;
    BYTE* rawBuffer = nullptr;

    // 阻塞取帧，超时 1000ms
    if (CameraGetImageBuffer(m_hCamera, &frameInfo, &rawBuffer, 1000)
        != CAMERA_STATUS_SUCCESS) {
        return false;
    }

    // SDK 图像处理（Bayer → BGR/灰度），写入预分配的内存
    CameraImageProcess(m_hCamera, rawBuffer, m_rgbBuf.data, &frameInfo);

    // 记录实际帧尺寸
    m_width  = frameInfo.iWidth;
    m_height = frameInfo.iHeight;

    // 构造 Mat（零拷贝，指向预分配内存）
    int type = (frameInfo.uiMediaType == CAMERA_MEDIA_TYPE_MONO8)
                   ? CV_8UC1 : CV_8UC3;
    frame = cv::Mat(cv::Size(m_width, m_height), type, m_rgbBuf.data);

    // 释放 SDK 帧缓存（⚠️ 必须调用，否则相机卡死）
    CameraReleaseImageBuffer(m_hCamera, rawBuffer);

    return true;
}

void Camera::release() {
    if (m_opened) {
        CameraUnInit(m_hCamera);
        m_opened = false;
        m_hCamera = -1;
        printf("[Camera] 已关闭\n");
    }
}
