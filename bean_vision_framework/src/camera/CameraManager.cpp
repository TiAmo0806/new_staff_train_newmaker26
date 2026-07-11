#include "camera/CameraManager.h"

#ifdef BVP_WITH_MINDVISION
#include <CameraApi.h>
#endif

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <iostream>
#include <memory>

namespace {

void applyImagePostProcess(const CameraConfig& config, cv::Mat& frame) {
    if (frame.empty()) {
        return;
    }

    if (config.flip_horizontal && config.flip_vertical) {
        cv::flip(frame, frame, -1);
    } else if (config.flip_horizontal) {
        cv::flip(frame, frame, 1);
    } else if (config.flip_vertical) {
        cv::flip(frame, frame, 0);
    }

    switch (config.rotate) {
    case 0:
        break;
    case 90:
        cv::rotate(frame, frame, cv::ROTATE_90_COUNTERCLOCKWISE);
        break;
    case 180:
        cv::rotate(frame, frame, cv::ROTATE_180);
        break;
    case 270:
        cv::rotate(frame, frame, cv::ROTATE_90_CLOCKWISE);
        break;
    default:
        std::cerr << "Unsupported camera.rotate value: " << config.rotate
                  << ". Use 0/90/180/270.\n";
        break;
    }
}

#ifdef BVP_WITH_MINDVISION
void logSdkWarning(const char* action, CameraSdkStatus status) {
    if (status != CAMERA_STATUS_SUCCESS) {
        std::cerr << "[WARN] " << action << " failed, status=" << status << "\n";
    }
}
#endif

}  // namespace

struct CameraManager::Impl {
#ifdef BVP_WITH_MINDVISION
    CameraHandle camera{-1};
    tSdkCameraCapbility capability{};
    std::unique_ptr<unsigned char[]> rgb_buffer;
#endif
};

CameraManager::CameraManager(const CameraConfig& config)
    : config_(config), impl_(std::make_unique<Impl>()) {}

CameraManager::~CameraManager() {
    close();
}

bool CameraManager::open() {
#ifdef BVP_WITH_MINDVISION
    CameraSdkInit(1);

    int camera_count = 16;
    tSdkCameraDevInfo camera_list[16];
    if (CameraEnumerateDevice(camera_list, &camera_count) != CAMERA_STATUS_SUCCESS || camera_count <= 0) {
        std::cerr << "No MindVision camera found.\n";
        return false;
    }

    if (config_.camera_id < 0 || config_.camera_id >= camera_count) {
        std::cerr << "MindVision camera index is out of range. Found " << camera_count << " camera(s).\n";
        return false;
    }

    if (CameraInit(&camera_list[config_.camera_id], -1, -1, &impl_->camera) != CAMERA_STATUS_SUCCESS) {
        std::cerr << "MindVision camera initialization failed.\n";
        return false;
    }

    if (CameraGetCapability(impl_->camera, &impl_->capability) != CAMERA_STATUS_SUCCESS) {
        std::cerr << "Failed to get MindVision camera capability.\n";
        close();
        return false;
    }

    // 输出 BGR8，后续 OpenCV/YOLO 流程可以直接使用。
    logSdkWarning("CameraSetIspOutFormat",
                  CameraSetIspOutFormat(impl_->camera, CAMERA_MEDIA_TYPE_BGR8));
    logSdkWarning("CameraSetMirror(horizontal)",
                  CameraSetMirror(impl_->camera, MIRROR_DIRECTION_HORIZONTAL, FALSE));
    logSdkWarning("CameraSetMirror(vertical)",
                  CameraSetMirror(impl_->camera, MIRROR_DIRECTION_VERTICAL, FALSE));
    logSdkWarning("CameraSetHardwareMirror(horizontal)",
                  CameraSetHardwareMirror(impl_->camera, MIRROR_DIRECTION_HORIZONTAL, FALSE));
    logSdkWarning("CameraSetHardwareMirror(vertical)",
                  CameraSetHardwareMirror(impl_->camera, MIRROR_DIRECTION_VERTICAL, FALSE));
    logSdkWarning("CameraSetAeState",
                  CameraSetAeState(impl_->camera, config_.auto_exposure ? TRUE : FALSE));
    if (!config_.auto_exposure) {
        logSdkWarning("CameraSetExposureTime",
                      CameraSetExposureTime(impl_->camera, config_.exposure_time));
    }
    if (!config_.auto_gain) {
        logSdkWarning("CameraSetAnalogGainX",
                      CameraSetAnalogGainX(impl_->camera, static_cast<float>(config_.gain)));
    }
    logSdkWarning("CameraSetWbMode",
                  CameraSetWbMode(impl_->camera, config_.auto_white_balance ? TRUE : FALSE));
    logSdkWarning("CameraSetTriggerMode", CameraSetTriggerMode(impl_->camera, 0));
    CameraPlay(impl_->camera);

    const int buffer_size =
        impl_->capability.sResolutionRange.iWidthMax * impl_->capability.sResolutionRange.iHeightMax * 3;
    impl_->rgb_buffer.reset(new unsigned char[buffer_size]);

    opened_ = true;
    std::cout << "MindVision camera opened, index: " << config_.camera_id << "\n";
    return true;
#else
    std::cerr << "MindVision camera support is not enabled in this build.\n";
    std::cerr << "Current build cannot open industrial camera with input.type=mindvision_camera.\n";
    std::cerr << "You need a build that links the MindVision SDK instead of falling back to /dev/videoX.\n";
    return false;
#endif
}

bool CameraManager::read(cv::Mat& frame) {
#ifdef BVP_WITH_MINDVISION
    if (!opened_ || impl_->camera < 0) {
        return false;
    }

    tSdkFrameHead frame_info;
    BYTE* raw_buffer = nullptr;
    if (CameraGetImageBuffer(impl_->camera, &frame_info, &raw_buffer, 1000) != CAMERA_STATUS_SUCCESS) {
        return false;
    }

    CameraImageProcess(impl_->camera, raw_buffer, impl_->rgb_buffer.get(), &frame_info);
    CameraReleaseImageBuffer(impl_->camera, raw_buffer);

    cv::Mat image(frame_info.iHeight, frame_info.iWidth, CV_8UC3, impl_->rgb_buffer.get());
    frame = image.clone();
    applyImagePostProcess(config_, frame);
    return true;
#else
    (void)frame;
    return false;
#endif
}

void CameraManager::close() {
#ifdef BVP_WITH_MINDVISION
    if (impl_ && impl_->camera >= 0) {
        CameraUnInit(impl_->camera);
        impl_->camera = -1;
    }
    if (impl_) {
        impl_->rgb_buffer.reset();
    }
#endif
    opened_ = false;
}

bool CameraManager::isOpened() const {
    return opened_;
}
