#ifndef MINDVISION_CAMERA_H
#define MINDVISION_CAMERA_H
#include "/home/zst/linuxSDK/include/CameraApi.h"
#include <opencv2/opencv.hpp>
#include <vector>

struct CameraConfig
{
    int index = 0;
    int exposureUs = 8000;
    int gain = 8;
    bool autoExposure = true;
};

class MindVisionCamera
{
public:
    explicit MindVisionCamera(const CameraConfig &config);
    ~MindVisionCamera();

    // 初始化 SDK、打开相机、设置输出 BGR8。
    bool open();

    // 释放相机句柄。
    void close();

    // 读取一帧图像，输出 OpenCV BGR Mat。
    bool read(cv::Mat &image);

    // 调试光照时用，比赛前建议固定曝光。
    void setExposureGain(int exposureUs, int gain);

private:
    CameraConfig config_;
    int handle_ = -1;
    tSdkCameraCapbility capability_{};
    std::vector<unsigned char> bgrBuffer_;
};

#endif // MINDVISION_CAMERA_H
