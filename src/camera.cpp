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
    rgb_buffer_(nullptr),
    is_recording_(false)
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

bool Camera::open(int width, int height)
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

    // 设置分辨率
    int maxW = cap.sResolutionRange.iWidthMax;
    int maxH = cap.sResolutionRange.iHeightMax;
    if (width > maxW) width = maxW;
    if (height > maxH) height = maxH;

    // 6. 判断黑白/彩色
    if (cap.sIspCapacity.bMonoSensor) {
        channel_ = 1;
        CameraSetIspOutFormat(hCamera_, CAMERA_MEDIA_TYPE_MONO8);
    } else {
        channel_ = 3;
        CameraSetIspOutFormat(hCamera_, CAMERA_MEDIA_TYPE_BGR8);
    }

    // 7. 分配 BGR 缓存
    rgb_buffer_ = (unsigned char*)malloc(maxW * maxH * 3);
    if (rgb_buffer_ == nullptr) {
        std::cerr << "图像缓存分配失败" << std::endl;
        CameraUnInit(hCamera_);
        hCamera_ = -1;
        return false;
    }

    // 8. 开始采集
    CameraPlay(hCamera_);

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

    // 获取一帧原始图像（超时 2000ms）
    int status = CameraGetImageBuffer(hCamera_, &frame_info, &raw_buffer, 2000);
    if (status != CAMERA_STATUS_SUCCESS) {
        // 每 30 次才印一次，避免刷屏
        static int timeoutCount = 0;
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

    // 录像
    if (is_recording_ && writer_.isOpened()) {
        writer_.write(mat);
    }

    return mat.clone();
}

void Camera::close()
{
    if (opened_ && hCamera_ != -1) {
        stopRecording();
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

bool Camera::isOpened() const
{
    return opened_;
}

void Camera::startRecording(const std::string& filename, int fps)
{
    if (!opened_) {
        std::cerr << "相机未打开，无法录像" << std::endl;
        return;
    }

    if (is_recording_) {
        stopRecording();
    }

    cv::Size size(width_, height_);
    writer_.open(filename, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), fps, size);
    
    if (writer_.isOpened()) {
        is_recording_ = true;
        std::cout << "开始录像: " << filename << std::endl;
    } else {
        std::cerr << "录像文件创建失败" << std::endl;
    }
}

void Camera::stopRecording()
{
    if (is_recording_) {
        writer_.release();
        is_recording_ = false;
        std::cout << "录像已停止" << std::endl;
    }
}