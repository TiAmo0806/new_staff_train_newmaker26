/*
 detection.cpp —— YOLO 检测工具函数实现
*/

#include "detection.hpp"
#include <opencv2/dnn.hpp>
#include <onnxruntime-sdk/include/onnxruntime_cxx_api.h>
#include <algorithm>
#include <iostream>

// ============================================================
//  1. 预处理函数（letterbox）
// ============================================================
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

    // 缩放到目标尺寸（保持宽高比）
    cv::Mat resized;
    cv::resize(image, resized, cv::Size(new_w, new_h));

    // 创建填充图像（BGR 填充 114,114,114，YOLO 标准）
    cv::Mat padded(input_h, input_w, CV_8UC3, cv::Scalar(114, 114, 114));
    resized.copyTo(padded(cv::Rect(dw, dh, new_w, new_h)));

    // 转换为 RGB 并归一化到 0~1
    cv::Mat rgb;
    cv::cvtColor(padded, rgb, cv::COLOR_BGR2RGB);
    cv::Mat float_img;
    rgb.convertTo(float_img, CV_32FC3, 1.0 / 255.0);

    // 转换为 NCHW 格式 (batch, channels, height, width)
    cv::Mat blob = cv::dnn::blobFromImage(float_img);

    return blob;
}

// ============================================================
//  2. 解析 YOLO 输出
// ============================================================
std::vector<Detection> parseYOLOv8Output(
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
    bool isFeatureFirst)
{
    std::vector<Detection> detections;
    int elemPerAnchor = numClasses + 4;  // 每个 anchor 的字段数

    for (int a = 0; a < numAnchors; ++a) {
        // 找到最大得分的类别
        int bestClass = -1;
        float bestScore = -1.0f;

        for (int c = 0; c < numClasses; ++c) {
            float score;
            if (isFeatureFirst) {
                // [classes+4, anchors]: 同一 anchor 的不同字段跨 numAnchors 个元素
                score = outputData[(4 + c) * numAnchors + a];
            } else {
                // [anchors, classes+4]: 同一 anchor 的 84 个字段连续存放
                score = outputData[a * elemPerAnchor + 4 + c];
            }
            if (score > bestScore) {
                bestScore = score;
                bestClass = c;
            }
        }

        // 过滤低置信度
        if (bestScore < confidence_threshold) {
            continue;
        }

        // 解析边界框 — 模型输出是输入空间像素坐标 (0~input_size)
        float cx, cy, w, h;
        if (isFeatureFirst) {
            cx = outputData[0 * numAnchors + a];
            cy = outputData[1 * numAnchors + a];
            w  = outputData[2 * numAnchors + a];
            h  = outputData[3 * numAnchors + a];
        } else {
            int base = a * elemPerAnchor;
            cx = outputData[base + 0];
            cy = outputData[base + 1];
            w  = outputData[base + 2];
            h  = outputData[base + 3];
        }

        // 输入空间 → 原图空间：先去 padding，再除以缩放比 r
        // x_original = (cx - w/2 - dw) / r = (cx - w/2 - dw) * scale
        float x1 = (cx - w / 2.0f - dw) * scale;
        float y1 = (cy - h / 2.0f - dh) * scale;
        float x2 = (cx + w / 2.0f - dw) * scale;
        float y2 = (cy + h / 2.0f - dh) * scale;

        int x = static_cast<int>(x1);
        int y = static_cast<int>(y1);
        int width = static_cast<int>(x2 - x1);
        int height = static_cast<int>(y2 - y1);

        // 边界检查
        x = std::max(0, std::min(x, imageWidth - 1));
        y = std::max(0, std::min(y, imageHeight - 1));
        width = std::max(1, std::min(width, imageWidth - x));
        height = std::max(1, std::min(height, imageHeight - y));

        detections.push_back({bestClass, bestScore, cv::Rect(x, y, width, height)});
    }

    // ---- NMS（非极大值抑制） ----
    std::sort(detections.begin(), detections.end(),
              [](const Detection& a, const Detection& b) {
                  return a.confidence > b.confidence;
              });

    std::vector<Detection> nms_result;
    nms_result.reserve(detections.size());

    for (size_t i = 0; i < detections.size(); ++i) {
        bool keep = true;
        float area_i = detections[i].bbox.area();
        for (size_t j = 0; j < nms_result.size(); ++j) {
            cv::Rect inter = detections[i].bbox & nms_result[j].bbox;
            float area_j = nms_result[j].bbox.area();
            float iou = static_cast<float>(inter.area()) / (area_i + area_j - inter.area());
            if (iou > nms_threshold) {
                keep = false;
                break;
            }
        }
        if (keep) {
            nms_result.push_back(detections[i]);
        }
    }

    return nms_result;
}

// ============================================================
//  3. 获取输出信息（自动计算 numClasses / numAnchors）
// ============================================================
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