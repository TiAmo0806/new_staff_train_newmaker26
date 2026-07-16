#include "ImgProcessing/YoloOpenVinoDetector.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>
#include <sstream>
#include <stdexcept>

namespace
{
// 类别编号就是训练数据集 YAML 中 names 的下标，顺序必须与导出best5.onnx时一致。
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

// 把OpenVINO张量形状转换成便于阅读的字符串，例如[1,3,640,640]。
// 初始化时打印真实输入和输出形状，可以快速发现模型导出尺寸或类别数不匹配。
std::string shapeToString(const ov::Shape &shape)
{
    std::ostringstream oss;
    oss << '[';
    for (std::size_t i = 0; i < shape.size(); ++i)
    {
        if (i != 0) oss << ',';
        oss << shape[i];
    }
    oss << ']';
    return oss.str();
}
}

YoloOpenVinoDetector::YoloOpenVinoDetector(const YoloConfig &config)
    : config_(config)
{
    if (!std::filesystem::is_regular_file(config_.modelPath))
        throw std::runtime_error("OpenVINO model file not found: " + config_.modelPath);

    // 创建缓存目录并交给OpenVINO。第一次启动会编译best5.onnx并写入缓存，
    // 后续启动若模型、设备和OpenVINO版本未变化，就可以直接复用已编译结果。
    if (!config_.cacheDir.empty())
    {
        std::error_code ec;
        std::filesystem::create_directories(config_.cacheDir, ec);
        if (ec)
            throw std::runtime_error("cannot create OpenVINO cache directory: " +
                                     config_.cacheDir + ": " + ec.message());
        core_.set_property(ov::cache_dir(config_.cacheDir));
    }

    // 只有用户在YAML中显式指定正数时才固定线程数；0完全交给OpenVINO自动选择。
    ov::AnyMap compileProperties;
    if (config_.intraOpThreads > 0)
        compileProperties.emplace(ov::inference_num_threads.name(), config_.intraOpThreads);

    // 直接把ONNX路径交给OpenVINO。这里没有ONNX Runtime，也不需要先转成.xml/.bin。
    // AUTO会在NUC上从已安装的Intel设备插件中自动选择；需要强制CPU时在YAML写CPU。
    compiledModel_ = core_.compile_model(config_.modelPath, config_.device, compileProperties);
    inferRequest_ = compiledModel_.create_infer_request();

    if (compiledModel_.inputs().size() != 1 || compiledModel_.outputs().size() != 1)
        throw std::runtime_error("best5.onnx must have exactly one input and one output");

    const ov::Output<const ov::Node> inputPort = compiledModel_.input();
    const ov::Output<const ov::Node> outputPort = compiledModel_.output();
    const ov::Shape inputShape = inputPort.get_shape();
    const ov::Shape outputShape = outputPort.get_shape();
    const ov::Shape expectedInput{1U, 3U,
                                  static_cast<std::size_t>(config_.inputHeight),
                                  static_cast<std::size_t>(config_.inputWidth)};
    if (inputShape != expectedInput)
        throw std::runtime_error("best5.onnx input shape " + shapeToString(inputShape) +
                                 " does not match configured " + shapeToString(expectedInput));
    if (inputPort.get_element_type() != ov::element::f32 ||
        outputPort.get_element_type() != ov::element::f32)
        throw std::runtime_error("best5.onnx input/output must be float32");
    if (outputShape.size() != 3)
        throw std::runtime_error("unexpected best5.onnx output shape: " +
                                 shapeToString(outputShape));
    // 本工程固定8类，所以YOLO输出的小维度必须严格等于4个框参数+8个类别=12。
    // 不能只检查“至少12”，否则误放入80类模型时会静默按错误类别表解析。
    const std::size_t outputChannels = std::min(outputShape[1], outputShape[2]);
    if (outputChannels != 4U + kClassNames.size())
        throw std::runtime_error("best5.onnx class count does not match project: output=" +
                                 shapeToString(outputShape) + ", expected 12 channels");

    std::cout << "[OpenVINO] 模型加载成功: " << config_.modelPath << std::endl;
    std::cout << "[OpenVINO] 设备=" << config_.device
              << "，输入=" << shapeToString(inputShape)
              << "，输出=" << shapeToString(outputShape) << std::endl;
    std::cout << "[OpenVINO] 编译缓存="
              << (config_.cacheDir.empty() ? std::string("关闭") : config_.cacheDir)
              << "，推理线程="
              << (config_.intraOpThreads > 0 ? std::to_string(config_.intraOpThreads)
                                             : std::string("auto"))
              << std::endl;
}

cv::Mat YoloOpenVinoDetector::letterbox(const cv::Mat &image, LetterBoxInfo &info) const
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

std::vector<Detection> YoloOpenVinoDetector::infer(const cv::Mat &frame)
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

    // 2. 把OpenCV生成的连续NCHW数据复制进OpenVINO输入张量。
    // InferRequest在构造时创建一次，每帧只更新输入并执行，不反复加载或编译模型。
    ov::Tensor inputTensor = inferRequest_.get_input_tensor();
    if (inputTensor.get_element_type() != ov::element::f32 ||
        inputTensor.get_size() != blob.total())
        throw std::runtime_error("OpenVINO input tensor no longer matches preprocessing output");
    std::memcpy(inputTensor.data<float>(), blob.ptr<float>(),
                blob.total() * sizeof(float));

    // 3. OpenVINO同步推理。输出一般形状是：
    //   [1, 12, 8400]  或 [1, 8400, 12]
    // 其中 12 = 4个框参数 + 8个类别分数。
    inferRequest_.infer();
    const ov::Tensor outputTensor = inferRequest_.get_output_tensor();
    const ov::Shape outputShape = outputTensor.get_shape();
    const std::vector<int64_t> shape(outputShape.begin(), outputShape.end());

    // outputTensor本身是const，因此取出的指针只能用于只读后处理。
    // OpenVINO 2025仍允许data<const float>()，但该模板写法已经弃用；
    // 使用data<float>()并赋给const float*同时兼容当前版本和2026.0新返回类型。
    const float *outputData = outputTensor.data<float>();
    return postprocess(outputData, shape, lb, frame.size());
}

std::vector<Detection> YoloOpenVinoDetector::postprocess(
    const float *data, const std::vector<int64_t> &shape,
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
    if (boxes <= 0 || channels != expectedChannels)
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
