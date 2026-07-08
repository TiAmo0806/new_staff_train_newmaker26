/**
 * detection.hpp —— YOLO 检测工具函数
 * 功能：预处理、解析输出、输出信息计算
 */

#ifndef DETECTION_HPP_
#define DETECTION_HPP_

#include <opencv2/opencv.hpp>
#include "onnxruntime-sdk/include/onnxruntime_cxx_api.h"

// ============================================================
//  检测结果结构体
// ============================================================
struct Detection
{
    int class_id;           // 类别索引
    float confidence;       // 置信度 (0~1)
    cv::Rect bbox;          // 边界框 (x, y, width, height)
};

// ============================================================
//  输出信息结构体
// ============================================================
struct OutputInfo
{
    int numClasses;                 // 类别数量
    int numAnchors;                 // Anchor 数量
    std::vector<int64_t> outputShape; // 输出形状
};

// ============================================================
//  1. 预处理函数（letterbox）
// ============================================================
/**
 * @param image     输入图像 (BGR)
 * @param dw        输出：填充宽度（左右各填充的像素数）
 * @param dh        输出：填充高度（上下各填充的像素数）
 * @param scale     输出：缩放比例（原图 → 模型输入）
 * @param input_w   模型输入宽度
 * @param input_h   模型输入高度
 * @return          NCHW 格式的 blob (float)
 */
cv::Mat preprocess(
    const cv::Mat& image,
    int& dw,
    int& dh,
    float& scale,
    int input_w,
    int input_h
);

// ============================================================
//  2. 解析 YOLO 输出
// ============================================================
/**
 * @param outputData             模型输出数据指针
 * @param numClasses             类别数量
 * @param numAnchors             Anchor 数量
 * @param imageWidth             原始图像宽度（用于边界裁剪）
 * @param imageHeight            原始图像高度（用于边界裁剪）
 * @param scale                  预处理时的缩放比例 (1/r)
 * @param dw                     预处理时的水平填充
 * @param dh                     预处理时的垂直填充
 * @param confidence_threshold   置信度阈值
 * @param nms_threshold          NMS IoU 阈值
 * @return                       检测结果列表
 *
 * 注意：此函数假设模型输出像素坐标（0~input_size），非归一化。
 */
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
    float nms_threshold
);

// ============================================================
//  3. 获取输出信息（自动计算 numClasses / numAnchors）
// ============================================================
/**
 * @param session   ONNX Runtime Session
 * @return          OutputInfo 结构体
 */
OutputInfo getOutputInfo(Ort::Session& session);

#endif  // DETECTION_HPP_