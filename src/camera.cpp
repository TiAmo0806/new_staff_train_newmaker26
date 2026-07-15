/*
 camera.cpp —— 迈德威视相机类实现
*/

#include "cemare.hpp"
#include <cstring>

Camera::Camera():
    hCamera_(-1),
    width_(640),
    height_(480),
    channel_(3),
    opened_(false),
    rgb_buffer_(nullptr)
{
    //SDK 全局初始化
    static bool sdk_inited = false;
    if (!sdk_inited) {
        CameraSdkInit(1);  //1=启用日志
        sdk_inited = true;
        std::cout << "MindVision SDK 初始化完成" << std::endl;
    }
}

Camera::~Camera()
{
    close();
}

bool Camera::open(int width, int height, double exposureTime, int analogGain)
{
    if (opened_) {
        close();
    }

    // 2. 枚举相机
    tSdkCameraDevInfo camera_list;
    int camera_count = 1;
    int status = CameraEnumerateDevice(&camera_list, &camera_count);
    
    if (status != CAMERA_STATUS_SUCCESS || camera_count == 0) {
        std::cerr << "未检测到迈德威视相机" << std::endl;
        return false;
    }

    // 3. 初始化相机
    status = CameraInit(&camera_list, -1, -1, &hCamera_);
    if (status != CAMERA_STATUS_SUCCESS) {
        std::cerr << "相机初始化失败，错误码: " << status << std::endl;
        return false;
    }

    // 获取相机能力（分辨率范围、传感器类型等）
    tSdkCameraCapbility cap;
    CameraGetCapability(hCamera_, &cap);

    // 仅做上限约束，不修改相机实际输出分辨率
    // 模型需要的 640x640 由 preprocess 的 letterbox 处理
    int maxW = cap.sResolutionRange.iWidthMax;
    int maxH = cap.sResolutionRange.iHeightMax;
    if (width > maxW) width = maxW;
    if (height > maxH) height = maxH;

    // 6. 判断黑白/彩色
    if (cap.sIspCapacity.bMonoSensor) {
        channel_ = 1;
        CameraSetIspOutFormat(hCamera_, CAMERA_MEDIA_TYPE_MONO8);
        //设置相机内部ISP处理后的输出图像格式
    } else {
        channel_ = 3;
        CameraSetIspOutFormat(hCamera_, CAMERA_MEDIA_TYPE_BGR8);
    }

    // 7. 分配 BGR 缓存
    rgb_buffer_ = (unsigned char*)malloc(maxW * maxH * 3);
    //C 标准库的内存分配函数，默认返回 void*
    if (rgb_buffer_ == nullptr) {
        std::cerr << "图像缓存分配失败" << std::endl;
        CameraUnInit(hCamera_);
        hCamera_ = -1;
        return false;
    }

    // 8. 开始采集
    CameraPlay(hCamera_);

    // 9. 保存并应用曝光参数
    exposureTime_ = exposureTime;
    analogGain_   = analogGain;
    if (exposureTime > 0) {
        CameraSetAeState(hCamera_, FALSE);               // 关闭自动曝光

        // 查询曝光时间范围（防止超出帧周期导致行数溢出）
        double expMin = 0, expMax = 0, expStep = 0;
        CameraGetExposureTimeRange(hCamera_, &expMin, &expMax, &expStep);
        double clampedExp = exposureTime;
        if (clampedExp > expMax) {
            std::cerr << "警告: 曝光 " << exposureTime
                      << " us 超出上限 " << expMax << " us，已自动修正" << std::endl;
            clampedExp = expMax;
        }
        if (clampedExp < expMin) clampedExp = expMin;

        CameraSetExposureTime(hCamera_, clampedExp);
        double actualExp = 0;
        CameraGetExposureTime(hCamera_, &actualExp);
        std::cout << "手动曝光: " << actualExp << " us"
                  << " (范围 " << expMin << "~" << expMax << " us, 步进 "
                  << expStep << " us)" << std::endl;
    }
    if (analogGain > 0) {
        CameraSetAnalogGain(hCamera_, analogGain);       // 模拟增益
        std::cout << "模拟增益: " << analogGain << std::endl;
    }

    width_ = width;
    height_ = height;
    opened_ = true;

    std::cout << "相机已打开 (分辨率 " << width_ << "x" << height_ 
              << ", " << (channel_ == 3 ? "彩色" : "黑白") << ")" << std::endl;
    return true;
}

cv::Mat Camera::getFrame()
{
    if (!opened_ || hCamera_ == -1) {
        return cv::Mat();
    }

    tSdkFrameHead frame_info;
    BYTE* raw_buffer = nullptr;
    //一种无符号字节类型（通常是 unsigned char 的别名）。表示“一个 8 位的二进制数据块”

    // 获取一帧原始图像（超时 2000ms）
    int status = CameraGetImageBuffer(hCamera_, &frame_info, &raw_buffer, 2000);
    if (status != CAMERA_STATUS_SUCCESS) {
        // 每 30 次才印一次，避免刷屏
        static int timeoutCount = 0;
        //静态局部变量。它只初始化一次，但它的值会在函数多次调用之间保持
        if (++timeoutCount % 30 == 0)
            std::cerr << "获取图像超时 (已连续" << timeoutCount << "次)" << std::endl;
        return cv::Mat();
    }

    // SDK 处理图像（Bayer插值、白平衡、转换到 BGR/MONO）
    CameraImageProcess(hCamera_, raw_buffer, rgb_buffer_, &frame_info);

    // 释放原始图像缓存
    CameraReleaseImageBuffer(hCamera_, raw_buffer);

    // 封装成 cv::Mat（注意：这里用的是 rgb_buffer_，需要 clone 一份）
    int type = (channel_ == 3) ? CV_8UC3 : CV_8UC1;
    cv::Mat mat(frame_info.iHeight, frame_info.iWidth, type, rgb_buffer_);

    // 如果实际分辨率不是我们设置的，更新宽高
    if (width_ != frame_info.iWidth || height_ != frame_info.iHeight) {
        width_ = frame_info.iWidth;
        height_ = frame_info.iHeight;
    }

    return mat.clone();
}

void Camera::close()
{
    if (opened_ && hCamera_ != -1) {
        CameraUnInit(hCamera_);
        hCamera_ = -1;
        opened_ = false;
        std::cout << "相机已关闭" << std::endl;
    }

    if (rgb_buffer_ != nullptr) {
        free(rgb_buffer_);
        rgb_buffer_ = nullptr;
    }
}

std::optional<cv::Mat> Camera::getFrameSafe(int emptyThreshold,
                                               int reconnectDelayMs)
{
    using namespace std::chrono;

    // ── WAITING 状态：等 delay 到了就 reopen ──
    if (camState_ == CamState::WAITING) {
        auto elapsed = duration_cast<milliseconds>(
            steady_clock::now() - waitStart_).count();
        if (elapsed >= reconnectDelayMs) {
            if (open(width_, height_, exposureTime_, analogGain_)) {
                std::cout << "相机重连成功" << std::endl;
                camState_ = CamState::NORMAL;
                emptyCount_ = 0;
            } else {
                waitStart_ = steady_clock::now();  // 重连失败，重置计时
                return std::nullopt;
            }
        } else {
            return std::nullopt;  // 还没到时间，跳过
        }
    }

    // ── NORMAL 状态：正常取帧 ──
    auto frame = getFrame();
    if (!frame.empty()) {
        emptyCount_ = 0;
        return frame;
    }

    emptyCount_++;
    if (emptyCount_ >= emptyThreshold) {
        std::cerr << "连续空帧，尝试重连相机..." << std::endl;
        close();
        camState_ = CamState::WAITING;
        waitStart_ = steady_clock::now();
        emptyCount_ = 0;
    }
    return std::nullopt;
}

bool Camera::isOpened() const
{
    return opened_;
}