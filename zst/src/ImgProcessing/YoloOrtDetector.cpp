#include "ImgProcessing/YoloOrtDetector.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numeric>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>
#include <stdexcept>

namespace
{
// 类别编号就是训练数据集 YAML 中 names 的下标，顺序必须与导出 best.onnx 时一致。
// 前 3 类是豆子，后 5 类是数字箱；若训练时改过 names 顺序，此处也必须同步修改。
const std::vector<std::string> kClassNames = {
    "soybean", "mung_bean", "white_kidney_bean",    // 0~2: 豆子类
    "data_1", "data_2", "data_3", "data_4", "data_5"}; // 3~7: 数字箱类

// IoU = 两框交集面积 / 两框并集面积，范围为 [0, 1]。
// NMS 用它判断两个候选框是否实际指向同一个目标。
float iou(const cv::Rect &a, const cv::Rect &b)
 {
    const int inter = (a & b).area();               // 交集面积
    const int uni = a.area() + b.area() - inter;    // 并集面积 = A + B - 交集
    return uni > 0 ? static_cast<float>(inter) / uni : 0.0f; // 避免除零
 }
}

YoloOrtDetector::YoloOrtDetector(const YoloConfig &config)
    : config_(config),
      env_(ORT_LOGGING_LEVEL_WARNING, "logistics_yolo"),     // 创建 ONNX 环境，WARNING 级别日志
      sessionOptions_(),
      session_(nullptr)
{
    // 0表示使用ONNX Runtime默认线程策略；现场也可在YAML中固定线程数做性能对比。
    if (config_.intraOpThreads > 0)
        sessionOptions_.SetIntraOpNumThreads(config_.intraOpThreads);

    // 开启 ONNX Runtime 图优化，能合并一些算子，CPU 推理会更快。
    sessionOptions_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL); // 启用全部安全图优化

    // 真正加载 .onnx 模型。如果路径错，这里会直接抛异常。
    session_ = Ort::Session(env_, config_.modelPath.c_str(), sessionOptions_); // 加载 ONNX 模型

    // 保存输入/输出节点名，Run 时必须传 const char*。
    // 大部分 YOLO 导出模型只有一个输入和一个输出。
    auto inputName = session_.GetInputNameAllocated(0, allocator_);   // 获取第一个输入节点名
    auto outputName = session_.GetOutputNameAllocated(0, allocator_); // 获取第一个输出节点名
    inputNamesText_.push_back(inputName.get());                       // 保存到 std::string 容器
    outputNamesText_.push_back(outputName.get());
    inputNames_.push_back(inputNamesText_[0].c_str());               // 取 c_str 供 Run() 使用
    outputNames_.push_back(outputNamesText_[0].c_str());

    std::cout << "[YoloOrt] model loaded: " << config_.modelPath << std::endl;
    std::cout << "[YoloOrt] intra-op threads: "
              << (config_.intraOpThreads > 0 ? std::to_string(config_.intraOpThreads)
                                             : std::string("auto"))
              << std::endl;
}

cv::Mat YoloOrtDetector::letterbox(const cv::Mat &image, LetterBoxInfo &info) const
{
    // 等比例缩放后补灰边，推理后再反算回原图坐标。
    //
    // 举例：
    //   原图 1280x1024，模型输入 640x640。
    //   不能直接压成 640x640，否则箱子和数字会被横向/纵向拉伸。
    //   letterbox 会缩放成 640x512，再上下补灰边。
    //   这样 YOLO 框的位置更稳定。
    const float scale = std::min(static_cast<float>(config_.inputWidth) / image.cols,   // 宽缩放比
                                 static_cast<float>(config_.inputHeight) / image.rows); // 高缩放比，取较小者
    const int newW = static_cast<int>(std::round(image.cols * scale));  // 缩放后宽度
    const int newH = static_cast<int>(std::round(image.rows * scale));  // 缩放后高度
    info.scale = scale;                                                 // 保存缩放比，后处理需要
    // 整数除法可能让右侧或下侧比另一边多 1 像素，这是奇数余量时的正常现象。
    info.padX = (config_.inputWidth - newW) / 2;                        // 水平灰边宽度
    info.padY = (config_.inputHeight - newH) / 2;                       // 垂直灰边宽度

    cv::Mat resized;
    cv::resize(image, resized, cv::Size(newW, newH));                   // 等比例缩放
    // 114 是 Ultralytics letterbox 常用填充值，能减少填充边缘与训练预处理的差异。
    cv::Mat out(config_.inputHeight, config_.inputWidth, CV_8UC3, cv::Scalar(114, 114, 114)); // 灰底画布
    resized.copyTo(out(cv::Rect(info.padX, info.padY, newW, newH)));   // 将缩放图贴到画布中央
    return out;
}

std::vector<Detection> YoloOrtDetector::infer(const cv::Mat &frame)
{
    if (frame.empty()) return {};

    // 1. 图像预处理：BGR -> RGB，HWC -> CHW，归一化到0~1。
    // blobFromImage在OpenCV内部完成通道交换、float转换和CHW排列，
    // 比逐像素执行三层C++循环更快，输出布局仍是[1,3,H,W]。
    LetterBoxInfo lb;
    cv::Mat input = letterbox(frame, lb);               // 等比例缩放 + 补灰边
    cv::Mat blob;
    cv::dnn::blobFromImage(input, blob, 1.0 / 255.0, cv::Size(), cv::Scalar(),
                           true, false, CV_32F);          // swapRB=true，输出NCHW float

    std::array<int64_t, 4> inputShape{1, 3, config_.inputHeight, config_.inputWidth}; // NCHW
    auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault); // CPU 内存

    // CreateTensor不复制数据，所以blob必须在Run结束前保持有效。
    Ort::Value tensor = Ort::Value::CreateTensor<float>(
        memoryInfo, blob.ptr<float>(), blob.total(), inputShape.data(), inputShape.size());

    // 2. ONNX Runtime CPU 推理。当前 Session 没有注册 CUDA Execution Provider，
    // 因此即使机器装有 CUDA 也仍走 CPU；若要 GPU 推理，需要 CUDA 版 ORT 并显式注册 Provider。
    // outputs[0] 一般形状是：
    //   [1, 12, 8400]  或 [1, 8400, 12]
    // 其中 12 = 4个框参数 + 8个类别分数。
    auto outputs = session_.Run(Ort::RunOptions{nullptr},
                                inputNames_.data(), &tensor, 1,          // 1 个输入
                                outputNames_.data(), 1);                 // 1 个输出
    const float *data = outputs[0].GetTensorData<float>();               // 获取输出数据指针
    auto shapeInfo = outputs[0].GetTensorTypeAndShapeInfo();
    return postprocess(data, shapeInfo.GetShape(), lb, frame.size());    // 后处理 + 坐标还原
}

std::vector<Detection> YoloOrtDetector::postprocess(const float *data, const std::vector<int64_t> &shape,
                                                    const LetterBoxInfo &info, const cv::Size &imageSize) const
{
    // 本解析器只接受 YOLOv8 检测模型的三维输出。若模型导出时内置 NMS，
    // 或导出的是分割/姿态模型，输出数量和形状不同，需要单独编写解析逻辑。
    if (shape.size() != 3) return {};                   // 非三维输出，无法解析

    // YOLOv8 常见输出：[1, 4+classes, boxes] 或 [1, boxes, 4+classes]。
    // 不同导出方式会转置维度，所以这里兼容两种情况。
    int boxes = 0;
    int channels = 0;
    bool transposed = false;
    // 对 640 输入，候选框数通常为 8400，通道数为 4+类别数（本项目为 12）。
    // 因此较小的维度可视为 channels，较大的维度可视为 boxes。
    if (shape[1] < shape[2])
    {
        channels = static_cast<int>(shape[1]);          // 较小维度 = 通道数
        boxes = static_cast<int>(shape[2]);             // 较大维度 = 候选框数
        transposed = true;                              // 转置布局：[1, C, N]
    }
    else
    {
        boxes = static_cast<int>(shape[1]);             // 较大维度 = 候选框数
        channels = static_cast<int>(shape[2]);          // 较小维度 = 通道数
    }

    // 本工程固定8类，所以至少需要4个框参数+8个类别分数。
    // 如果误换成分割/姿态/其他类别数模型，直接报错而不是越界读取张量。
    const int expectedChannels = 4 + static_cast<int>(kClassNames.size());
    if (boxes <= 0 || channels < expectedChannels)
        throw std::runtime_error("unexpected YOLO output shape: channels=" +
                                 std::to_string(channels) + ", boxes=" +
                                 std::to_string(boxes));

    std::vector<cv::Rect> rects;
    std::vector<float> scores;
    std::vector<int> classIds;

    for (int i = 0; i < boxes; ++i)
    {
        // 取一个候选框中分数最高的类别。
        // YOLOv8 导出的 ONNX 通常已经没有 objectness 维度，
        // 所以类别分数就是最终置信度。
        // 用统一访问器屏蔽 [1,C,N] 与 [1,N,C] 两种内存布局。
        auto valueAt = [&](int c) {
            return transposed ? data[c * boxes + i] : data[i * channels + c]; // 统一访问
        };

        const float cx = valueAt(0);                    // 框中心 x（模型坐标）
        const float cy = valueAt(1);                    // 框中心 y（模型坐标）
        const float w = valueAt(2);                     // 框宽度（模型坐标）
        const float h = valueAt(3);                     // 框高度（模型坐标）

        // 一个候选框保留得分最高的类别，再用 confThreshold 过滤。
        int bestClass = -1;
        float bestScore = 0.0f;
        for (int c = 0; c < static_cast<int>(kClassNames.size()); ++c)
        {
            float s = valueAt(4 + c);                   // 第 c 类的置信度
            if (s > bestScore)
            {
                bestScore = s;                          // 更新最高分
                bestClass = c;                          // 更新最佳类别
            }
        }
        if (bestScore < config_.confThreshold) continue; // 低于置信度阈值，丢弃

        // 模型输出是 letterbox 图上的 cx/cy/w/h。
        // 先转成 x1/y1/x2/y2，再减掉灰边，最后除以缩放比例。
        float x1 = (cx - w * 0.5f - info.padX) / info.scale; // 还原左上角 x
        float y1 = (cy - h * 0.5f - info.padY) / info.scale; // 还原左上角 y
        float x2 = (cx + w * 0.5f - info.padX) / info.scale; // 还原右下角 x
        float y2 = (cy + h * 0.5f - info.padY) / info.scale; // 还原右下角 y
        x1 = std::clamp(x1, 0.0f, static_cast<float>(imageSize.width - 1));  // 裁剪到图像范围内
        y1 = std::clamp(y1, 0.0f, static_cast<float>(imageSize.height - 1));
        x2 = std::clamp(x2, 0.0f, static_cast<float>(imageSize.width - 1));
        y2 = std::clamp(y2, 0.0f, static_cast<float>(imageSize.height - 1));

        cv::Rect r(cv::Point(static_cast<int>(x1), static_cast<int>(y1)),
                   cv::Point(static_cast<int>(x2), static_cast<int>(y2)));
        if (r.area() <= 0) continue;                    // 无效框，跳过

        rects.push_back(r);                             // 保存框坐标
        scores.push_back(bestScore);                    // 保存置信度
        classIds.push_back(bestClass);                  // 保存类别编号
    }

    // 必须先按置信度降序排列：NMS 永远优先保留更可信的框，再抑制低分重叠框。
    std::vector<int> order(rects.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return scores[a] > scores[b]; });

    // 按置信度从高到低做 NMS，去掉同类别重复框。
    //
    // nmsThreshold 越小，去重越严格；
    // 例如 0.20 会比 0.50 更容易删掉重叠框。
    // 这里按”同类别”做 NMS，避免豆子框把数字框误删。
    std::vector<int> keep;
    for (int idx : order)
    {
        bool suppressed = false;
        for (int kept : keep)
        {
            if (classIds[idx] == classIds[kept] && iou(rects[idx], rects[kept]) > config_.nmsThreshold)
            {
                suppressed = true;                  // 同类别且 IoU 过高，抑制
                break;
            }
        }
        if (!suppressed) keep.push_back(idx);       // 未被抑制，保留
    }

    // 将模型类别编号转换为业务层语义，后续 SVM、投票器和串口层无需理解张量。
    std::vector<Detection> detections;
    for (int idx : keep)
    {
        Detection d;
        d.classId = classIds[idx];
        d.score = scores[idx];
        d.box = rects[idx];
        d.label = kClassNames[d.classId];
        if (isBeanClass(d.classId))
        {
            d.kind = TargetKind::Bean;
            d.bean = classIdToBean(d.classId);
        }
        else if (isDigitClass(d.classId))
        {
            d.kind = TargetKind::DigitBox;
            d.digit = classIdToDigit(d.classId);
        }
        detections.push_back(d);
    }
    return detections;
}
