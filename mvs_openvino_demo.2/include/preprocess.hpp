#ifndef PREPROCESS_HPP
#define PREPROCESS_HPP

#include <opencv2/opencv.hpp>
#include <openvino/openvino.hpp>

/**
 * 图像预处理：BGR → RGB → Resize(640×640) → float32 归一化 [0,1]
 *
 * @param frame  输入图像（BGR, uint8）
 * @return       预处理后的图像（RGB, float32, HWC 布局, 640×640）
 */
cv::Mat preprocess(const cv::Mat& frame);

/**
 * HWC → CHW 布局转换，封装为 OpenVINO Tensor
 *
 * @param blob  预处理后的图像（HWC, float32, 640×640×3）
 * @return      OpenVINO Tensor（NCHW, [1, 3, 640, 640]）
 */
ov::Tensor blob_to_tensor(const cv::Mat& blob);

#endif // PREPROCESS_HPP
