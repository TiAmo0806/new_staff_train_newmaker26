#ifndef MINDVISION_CAMERA_HPP
#define MINDVISION_CAMERA_HPP

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <mutex>

#include "CameraApi.h"
#include "CameraDefine.h"
#include "CameraStatus.h"

class MindVisionCamera {
public:
    MindVisionCamera();
    ~MindVisionCamera();

    bool open(int camera_index = 0);
    void close();
    bool isOpened() const { return m_is_opened; }
    bool read(cv::Mat& frame);
    
    void setExposureTime(int exposure_us);
    void setGain(int gain);
    void setResolution(int width, int height);

private:
    int m_device_id = -1;
    bool m_is_opened = false;
    int m_image_width = 0;
    int m_image_height = 0;
    std::mutex m_mutex;
};

#endif