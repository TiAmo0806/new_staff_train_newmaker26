#ifndef VISUALIZE_HPP
#define VISUALIZE_HPP

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>

struct Detection;

/**
 * 在图像上绘制检测框、类别名、置信度
 *
 * @param frame       被绘制的图像（原地修改）
 * @param detections  检测结果列表
 */
void drawDetections(cv::Mat& frame, const std::vector<Detection>& detections);

/**
 * 获取预定义的类别颜色表
 */
const std::vector<cv::Scalar>& getClassColors();

/**
 * 设置类别名称（用于绘制时显示）
 */
void setClassNames(const std::vector<std::string>& names);

#endif // VISUALIZE_HPP
