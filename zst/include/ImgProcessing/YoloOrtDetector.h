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
    // YOLOv8 训练完成后导出的 ONNX 模型路径，例如 best.onnx。
    // 这里加载的不是 PyTorch 的 .pt 文件；部署时不需要 Python/Ultralytics，
    // 只需要 ONNX Runtime、模型文件以及与训练阶段完全一致的类别顺序。
    std::string modelPath;

    // 模型输入张量的宽和高，默认对应 [1, 3, 640, 640]。
    // 必须与导出 ONNX 时的 imgsz 一致，否则固定尺寸模型会在 Run() 时报维度错误。
    int inputWidth = 640;
    int inputHeight = 640;

    // 候选框最低类别置信度。小于该值的框会在 NMS 之前被直接丢弃。
    // 调高可减少误检，调低可减少漏检，比赛现场应结合验证集和实际光照调整。
    float confThreshold = 0.35f;

    // 非极大值抑制（NMS）的 IoU 阈值，只抑制“同类别”的重叠框。
    // 阈值越低，去重越严格；过低时可能把靠得很近的两个真实目标误删。
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

    YoloConfig config_;                    // 推理尺寸、阈值及模型路径的只读副本
    Ort::Env env_;
    Ort::SessionOptions sessionOptions_;
    Ort::Session session_;                 // 已加载的 ONNX 计算图；每帧复用，不能重复创建
    Ort::AllocatorWithDefaultOptions allocator_;
    // Text 容器负责字符串生命周期，const char* 容器仅供 Session::Run 调用。
    // 如果只保存 GetInputNameAllocated() 返回的临时指针，离开构造函数后会悬空。
    std::vector<std::string> inputNamesText_;
    std::vector<std::string> outputNamesText_;
    std::vector<const char *> inputNames_;
    std::vector<const char *> outputNames_;
};

#endif // YOLO_ORT_DETECTOR_H
