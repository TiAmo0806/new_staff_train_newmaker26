/*
 detection.cpp —— YOLO 检测工具函数实现
*/

#include "detection.hpp"
#include <opencv2/dnn.hpp>
#include <onnxruntime-sdk/include/onnxruntime_cxx_api.h>
#include <algorithm>
#include <iostream>

//  1. 预处理函数（letterbox）
cv::Mat preprocess(
    const cv::Mat& image,
    int& dw,
    int& dh,
    float& scale,
    int input_w,
    int input_h)
{
    // 计算缩放比例（保持宽高比）
    float r = std::min(
        static_cast<float>(input_w) / image.cols,
        static_cast<float>(input_h) / image.rows
    );
    int new_w = static_cast<int>(image.cols * r);
    int new_h = static_cast<int>(image.rows * r);
    dw = (input_w - new_w) / 2;
    dh = (input_h - new_h) / 2;
    scale = 1.0f / r;
    /*
    r：缩比率。比如原图是 1920x1080，要缩到 640x640。宽要缩 640/1920=0.33，高要缩 640/1080=0.59，取最小的 0.33。防止图片变形，如果取大的，图片会超出画布被裁掉。
    new_w, new_h：按 0.33 缩放后的实际尺寸（633x356）。
    dw, dh：左右上下的灰边宽度。(640 - 633)/2 = 3，(640 - 356)/2 = 142。
    scale：“还原放大镜”。因为模型输出的是 640x640 画布里的坐标，要还原成 1920x1080 原图坐标，需要除以 r，也就是乘以 1/r。这是后面解码的关键钥匙。
    */

    // ---- CLAHE 直方图均衡化（增强光照鲁棒性）----
    cv::Mat lab;
    cv::cvtColor(image, lab, cv::COLOR_BGR2Lab);
    std::vector<cv::Mat> lab_channels;
    cv::split(lab, lab_channels);
    auto clahe = cv::createCLAHE(2.0, cv::Size(8, 8));
    clahe->apply(lab_channels[0], lab_channels[0]);  // 只均衡 L（亮度）通道
    cv::merge(lab_channels, lab);
    cv::Mat equalized;
    cv::cvtColor(lab, equalized, cv::COLOR_Lab2BGR);

    cv::Mat resized;
    cv::resize(equalized, resized, cv::Size(new_w, new_h));

    // 创建填充图像（BGR 填充 114,114,114，YOLO 标准）
    cv::Mat padded(input_h, input_w, CV_8UC3, cv::Scalar(114, 114, 114));
    // 114是 YOLO 官方在 COCO 数据集上统计出的图像平均背景色。用灰色填充，比用纯黑（0）填充，对神经网络推理的干扰更小。
    resized.copyTo(padded(cv::Rect(dw, dh, new_w, new_h)));
    //在padded上划出一块roi区域，把图片拷贝到roi上
    // 转换为 RGB 并归一化到 0~1
    cv::Mat rgb;
    cv::cvtColor(padded, rgb, cv::COLOR_BGR2RGB);
    cv::Mat float_img;
    rgb.convertTo(float_img, CV_32FC3, 1.0 / 255.0);

    // 转换为 NCHW 格式 (batch, channels, height, width)
    cv::Mat blob = cv::dnn::blobFromImage(float_img);
    /*
    调整维度顺序：普通图片在内存里是 HWC（高、宽、通道）。但 ONNX 模型要求 NCHW（数量、通道、高、宽）。
    增加 Batch 维度：在最前面加了个 1（代表这一批只推理这一张图）。
    */
    return blob;
}


//  2a. NMS（非极大值抑制）—— 两个解析分支共用
/*
static：这是一个文件作用域的修饰符。加上 static 后，这个函数只能在这个 .cpp 文件内部被调用，
外部（其他 .cpp）看不见它，也链接不到它。这是 C++ 里实现“内部工具函数”的标准做法，
防止和别人写的同名函数冲突。
*/
static std::vector<Detection> applyNMS(
    std::vector<Detection>& detections,
    float nms_threshold)
{
    if (detections.empty()) {
        return {};
    }

    std::sort(detections.begin(), detections.end(),
              [](const Detection& a, const Detection& b) {
                  return a.confidence > b.confidence;
              });
    /*
    std::sort 的工作原理：它需要一个“比较器”，这个比较器必须返回 bool 类型。
    如果返回 true：表示 a 应该排在 b 的前面。
    如果返回 false：表示 b 应该排在 a 的前面
    */
    std::vector<Detection> nms_result;
    nms_result.reserve(detections.size());

    for (size_t i = 0; i < detections.size(); ++i) {
        bool keep = true;
        float area_i = detections[i].bbox.area();
        for (size_t j = 0; j < nms_result.size(); ++j) {
            cv::Rect inter = detections[i].bbox & nms_result[j].bbox;
            //OpenCV 的 Rect 类重载了 & 运算符，直接返回两个矩形框重叠的部分。
            if (inter.area() == 0) continue;  // 无交集，直接跳过 IoU 计算（小优化）
            float area_j = nms_result[j].bbox.area();
            float iou = static_cast<float>(inter.area()) / (area_i + area_j - inter.area());
            //iou：交并比公式 交集面积 / (面积1 + 面积2 - 交集面积)。
            if (iou > nms_threshold) {
                keep = false;
                break;
            }
        }
        if (keep) {
            nms_result.push_back(detections[i]);
        //nms_result 一开始是空的，所以第一个框（置信度最高的）不会进入内层循环，keep 保持 true，直接入榜。
        }
    }

    return nms_result;
}

//  2b. 解析 YOLO 输出 —— 特征在前 [classes+4, anchors]（YOLOv8/v11 官方格式）
std::vector<Detection> parseYOLOv8OutputFeatureFirst(
    const float* outputData,
    int numClasses,
    int numAnchors,
    int imageWidth,
    int imageHeight,
    float scale,
    int dw,
    int dh,
    float confidence_threshold,
    float nms_threshold,
    int usedClasses)
{
    std::vector<Detection> detections;
    detections.reserve(numAnchors);  // 预分配，避免频繁扩容

    int maxClass = (usedClasses > 0) ? std::min(numClasses, usedClasses) : numClasses;

    for (int a = 0; a < numAnchors; ++a) {
        // ---- 找最大类别分数 ----
        int bestClass = -1;
        float bestScore = -1.0f;
        for (int c = 0; c < maxClass; ++c) {
            float score = outputData[(4 + c) * numAnchors + a];
            if (score > bestScore) {
                bestScore = score;
                bestClass = c;
            }
        }

        if (bestScore < confidence_threshold) continue;

        // ---- 取坐标 ----
        float cx = outputData[0 * numAnchors + a];
        float cy = outputData[1 * numAnchors + a];
        float w  = outputData[2 * numAnchors + a];
        float h  = outputData[3 * numAnchors + a];

        // ---- 坐标还原到原图 ----
        float x1 = (cx - w * 0.5f - dw) * scale;
        float y1 = (cy - h * 0.5f - dh) * scale;
        float x2 = (cx + w * 0.5f - dw) * scale;
        float y2 = (cy + h * 0.5f - dh) * scale;

        int x = static_cast<int>(x1);
        int y = static_cast<int>(y1);
        int width = static_cast<int>(x2 - x1);
        int height = static_cast<int>(y2 - y1);

        // ---- 边界裁剪 ----
        x = std::max(0, std::min(x, imageWidth - 1));
        y = std::max(0, std::min(y, imageHeight - 1));
        width = std::max(1, std::min(width, imageWidth - x));
        height = std::max(1, std::min(height, imageHeight - y));

        detections.push_back({bestClass, bestScore, cv::Rect(x, y, width, height)});
    }

    return applyNMS(detections, nms_threshold);
}

//  2c. 解析 YOLO 输出 —— 锚点在前 [anchors, classes+4]（某些第三方导出格式）
std::vector<Detection> parseYOLOv8OutputAnchorFirst(
    const float* outputData,
    int numClasses,
    int numAnchors,
    int imageWidth,
    int imageHeight,
    float scale,
    int dw,
    int dh,
    float confidence_threshold,
    float nms_threshold,
    int usedClasses)
{
    std::vector<Detection> detections;
    detections.reserve(numAnchors);  // 预分配，避免频繁扩容

    int elemPerAnchor = numClasses + 4;  // 每个 anchor 的字段数
    int maxClass = (usedClasses > 0) ? std::min(numClasses, usedClasses) : numClasses;

    for (int a = 0; a < numAnchors; ++a) {
        int base = a * elemPerAnchor;  // 第 a 个框的起始地址

        // ---- 找最大类别分数 ----
        int bestClass = -1;
        float bestScore = -1.0f;
        for (int c = 0; c < maxClass; ++c) {
            float score = outputData[base + 4 + c];
            if (score > bestScore) {
                bestScore = score;
                bestClass = c;
            }
        }

        if (bestScore < confidence_threshold) continue;

        // ---- 取坐标 ----
        float cx = outputData[base + 0];
        float cy = outputData[base + 1];
        float w  = outputData[base + 2];
        float h  = outputData[base + 3];

        // ---- 坐标还原到原图 ----
        float x1 = (cx - w * 0.5f - dw) * scale;
        float y1 = (cy - h * 0.5f - dh) * scale;
        float x2 = (cx + w * 0.5f - dw) * scale;
        float y2 = (cy + h * 0.5f - dh) * scale;

        int x = static_cast<int>(x1);
        int y = static_cast<int>(y1);
        int width = static_cast<int>(x2 - x1);
        int height = static_cast<int>(y2 - y1);

        // ---- 边界裁剪 ----
        x = std::max(0, std::min(x, imageWidth - 1));
        y = std::max(0, std::min(y, imageHeight - 1));
        width = std::max(1, std::min(width, imageWidth - x));
        height = std::max(1, std::min(height, imageHeight - y));

        detections.push_back({bestClass, bestScore, cv::Rect(x, y, width, height)});
    }

    return applyNMS(detections, nms_threshold);
}
//  3. 获取输出信息（自动计算 numClasses / numAnchors）
OutputInfo getOutputInfo(Ort::Session& session)
{
    OutputInfo info;
    try {
        // 获取输出形状
        auto type_info = session.GetOutputTypeInfo(0);
        auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
        info.outputShape = tensor_info.GetShape();

        // 根据输出形状计算
        // 常见格式1: [batch, num_anchors, num_classes + 4]
        // 常见格式2: [batch, num_classes + 4, num_anchors]
        if (info.outputShape.size() == 3) {
            int dim1 = static_cast<int>(info.outputShape[1]);
            int dim2 = static_cast<int>(info.outputShape[2]);

            // 通常 num_classes 在 10~200 之间，num_anchors 在 1000~10000 之间
            if (dim1 > dim2) {
                // [batch, anchors, classes+4]
                info.numAnchors = dim1;
                info.numClasses = dim2 - 4;
                info.isFeatureFirst = false;
            } else {
                // [batch, classes+4, anchors]
                info.numClasses = dim1 - 4;
                info.numAnchors = dim2;
                info.isFeatureFirst = true;
            }
        } else {
            std::cerr << "未知的输出形状，使用默认值" << std::endl;
            info.numClasses = 3;
            info.numAnchors = 8400;
        }

        std::cout << "输出信息:" << std::endl;
        std::cout << "   - 形状: ";
        for (auto d : info.outputShape) std::cout << d << " ";
        std::cout << std::endl;
        std::cout << "   - numClasses: " << info.numClasses << std::endl;
        std::cout << "   - numAnchors: " << info.numAnchors << std::endl;

    } catch (const Ort::Exception& e) {
        std::cerr << "获取输出信息失败: " << e.what() << std::endl;
        info.numClasses = 3;
        info.numAnchors = 8400;
    }

    return info;
}