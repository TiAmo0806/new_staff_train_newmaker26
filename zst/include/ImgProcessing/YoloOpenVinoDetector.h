#ifndef YOLO_OPENVINO_DETECTOR_H
#define YOLO_OPENVINO_DETECTOR_H

#include "ImgProcessing/VisionTypes.h"
#include <cstdint>
#include <openvino/openvino.hpp>
#include <opencv2/core.hpp>
#include <string>
#include <vector>

struct YoloConfig
{
    // YOLO训练完成后导出的ONNX模型路径，例如best5.onnx。
    // 这里加载的不是 PyTorch 的 .pt 文件；部署时不需要 Python/Ultralytics，
    // 只需要OpenVINO Runtime、模型文件以及与训练阶段完全一致的类别顺序。
    std::string modelPath;

    // OpenVINO编译设备。AUTO会优先选择当前NUC上可用的Intel设备，
    // 没有可用GPU/NPU插件时会自动回退CPU；也可以在YAML中固定为CPU。
    std::string device = "AUTO";

    // OpenVINO模型编译缓存目录。第一次运行会编译ONNX并写入缓存，
    // 第二次及以后可复用缓存，从而显著缩短模型启动时间。
    std::string cacheDir = "runtime/openvino_cache";

    // 模型输入张量的宽和高，默认对应 [1, 3, 640, 640]。
    // 必须与导出 ONNX 时的 imgsz 一致，否则固定尺寸模型会在 Run() 时报维度错误。
    int inputWidth = 640;
    int inputHeight = 640;

    // OpenVINO推理线程数；0表示由OpenVINO根据设备自动选择。
    int intraOpThreads = 0;

    // 候选框最低类别置信度。小于该值的框会在 NMS 之前被直接丢弃。
    // 调高可减少误检，调低可减少漏检，比赛现场应结合验证集和实际光照调整。
    float confThreshold = 0.35f;

    // 非极大值抑制（NMS）的 IoU 阈值，只抑制“同类别”的重叠框。
    // 阈值越低，去重越严格；过低时可能把靠得很近的两个真实目标误删。
    float nmsThreshold = 0.20f;
};

class YoloOpenVinoDetector
{
public:
    // 直接读取ONNX并编译到OpenVINO设备；对象创建后每帧复用同一个InferRequest。
    explicit YoloOpenVinoDetector(const YoloConfig &config);

    // 输入一帧 BGR 图像，输出 YOLO 检测框。
    // 输入：工业相机原图。
    // 输出：已经完成坐标还原和 NMS 去重的 Detection。
    std::vector<Detection> infer(const cv::Mat &frame);

private:
    struct LetterBoxInfo
    {
        // 原图乘 scale 后得到等比例缩放图，再放到模型输入画布中央。
        // padX/padY 是画布左侧、上侧的灰边宽度，后处理反算坐标时必须扣除。
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

    YoloConfig config_;                  // 输入尺寸、设备、缓存、阈值和模型路径
    ov::Core core_;                      // OpenVINO运行时入口：读取模型、查询设备、编译模型
    ov::CompiledModel compiledModel_;    // 已针对CPU/AUTO设备编译的模型，可复用缓存
    ov::InferRequest inferRequest_;      // 每帧复用的同步推理请求，避免反复分配
};

#endif // YOLO_OPENVINO_DETECTOR_H
