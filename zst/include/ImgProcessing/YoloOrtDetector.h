#ifndef YOLO_ORT_DETECTOR_H
#define YOLO_ORT_DETECTOR_H

#include "/home/zst/zst/include/ImgProcessing/VisionTypes.h"
#include "/home/zst/onnxruntime/include/onnxruntime_cxx_api.h"
#include <cstdint>
#include <opencv2/core.hpp>
#include <string>
#include <vector>

struct YoloConfig
{
    // ONNX 模型路径，建议使用桌面训练导出的 best.onnx。
    std::string modelPath;

    // YOLO 输入尺寸，必须和导出模型一致。
    int inputWidth = 640;
    int inputHeight = 640;

    // 置信度越高，误检越少，但漏检可能增加。
    float confThreshold = 0.35f;

    // NMS 阈值越低越严格，重复框越少。
    float nmsThreshold = 0.20f;
};

class YoloOrtDetector
{
public:
    // 加载 ONNX Runtime CPU 模型。
    // 虚拟机 CPU 环境下优先保证能稳定运行。
    explicit YoloOrtDetector(const YoloConfig &config);

    // 输入一帧 BGR 图像，输出 YOLO 检测框。
    // 输入：工业相机原图。
    // 输出：已经完成坐标还原和 NMS 去重的 Detection。
    std::vector<Detection> infer(const cv::Mat &frame);

private:
    struct LetterBoxInfo
    {
        float scale = 1.0f;
        int padX = 0;
        int padY = 0;
    };

    // 保持比例缩放，避免直接 resize 拉变形。
    // YOLO 需要固定输入尺寸，例如 640x640；
    // 相机图像可能是 1280x1024，比例不同。
    // 所以先等比例缩放，再补灰边。
    cv::Mat letterbox(const cv::Mat &image, LetterBoxInfo &info) const;

    // 解析 YOLOv8 输出，并做 NMS 去重。
    // 这里会把模型输出的 cx/cy/w/h 转回原图 x/y/w/h。
    std::vector<Detection> postprocess(const float *data, const std::vector<int64_t> &shape,
                                       const LetterBoxInfo &info, const cv::Size &imageSize) const;

    YoloConfig config_;
    Ort::Env env_;
    Ort::SessionOptions sessionOptions_;
    Ort::Session session_;
    Ort::AllocatorWithDefaultOptions allocator_;
    std::vector<std::string> inputNamesText_;
    std::vector<std::string> outputNamesText_;
    std::vector<const char *> inputNames_;
    std::vector<const char *> outputNames_;
};

#endif // YOLO_ORT_DETECTOR_H
