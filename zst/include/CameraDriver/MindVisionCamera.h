#ifndef MINDVISION_CAMERA_H
#define MINDVISION_CAMERA_H                  
//头文件保护(防止头文件被重复 include 导致重定义)
// CameraApi.h 属于 MindVision SDK，由 CMake 的 MINDVISION_INCLUDE_DIR 提供搜索路径。
// 不在源码中写死 /home/某个用户/...，这样工程移动到任意 NUC 用户目录都能编译。
#include <CameraApi.h>
#include <opencv2/opencv.hpp>
#include <vector>

struct CameraConfig
{
    int exposureUs = 8000;      // 手动曝光时间，单位微秒
    int gain = 8;               // 模拟增益，数值越大画面越亮但噪点越多
    bool autoExposure = true;   // true 表示使用自动曝光，忽略手动参数
    int frameTimeoutMs = 200;   // 单次等待相机帧的最长时间；过大时掉线会长时间假死
    int reconnectAfterFailures = 5; // 连续取帧失败多少次后由main关闭并重新打开相机
};

class MindVisionCamera
{
public:
    explicit MindVisionCamera(const CameraConfig &config);
    //防止构造函数发生隐式转换
    ~MindVisionCamera();

    // 初始化 SDK、打开相机、设置输出 BGR8。
    bool open();

    // 释放相机句柄。
    void close();

    // 读取一帧图像，输出 OpenCV BGR Mat。返回的Mat引用内部缓存，下一次read前有效。
    bool read(cv::Mat &image);

    // 调试光照时用，比赛前建议固定曝光。
    void setExposureGain(int exposureUs, int gain);

private:
    CameraConfig config_;                       // 曝光/增益配置的只读副本
    bool sdkInitialized_ = false;               // SDK全局初始化只做一次，重连时只重开设备
    int handle_ = -1;                           // MindVision SDK 相机句柄，-1 表示未打开
    tSdkCameraCapbility capability_{};         // 相机能力参数：最大分辨率等
    std::vector<unsigned char> bgrBuffer_;     // BGR 图像缓存，每帧复用避免反复分配
};

#endif // MINDVISION_CAMERA_H
