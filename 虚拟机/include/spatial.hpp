#ifndef SPATIAL_HPP
#define SPATIAL_HPP

#include "detector.hpp"   // Detection 结构体

#include <vector>
#include <string>
#include <opencv2/opencv.hpp>

/**
 * 检测对象的中心坐标（从 Detection 提取）
 */
struct ObjectCenter {
    int    class_id;       // 类别 ID
    std::string class_name; // 类别名称
    float  cx, cy;         // 中心坐标

    /// 从 Detection 构造，自动计算中心点
    static ObjectCenter fromDetection(const Detection& d);
};

/**
 * 按 X 坐标排序器
 *
 * 将检测到的物体按中心点 X 坐标从小到大（从左到右）排序，
 * 输出排序后的类别 ID 序列。
 */
class SpatialSorter {
public:
    /**
     * 按中心 X 坐标从左到右排序
     *
     * @param detections  模型检测结果
     * @return            按 X 坐标升序排列的检测结果
     */
    static std::vector<Detection> sortLeftToRight(
        const std::vector<Detection>& detections);

    /**
     * 提取中心点并按 X 坐标升序排列
     */
    static std::vector<ObjectCenter> sortedCenters(
        const std::vector<Detection>& detections);

    /**
     * 将排序后的检测结果格式化为序列字符串
     * 例如: "2413" 表示从左到右依次是 class_id=2,4,3,1
     */
    static std::string formatOrder(const std::vector<Detection>& sorted);
};

/**
 * 在图像上绘制中心点
 */
void drawCenters(cv::Mat& frame,
                 const std::vector<ObjectCenter>& centers);

#endif // SPATIAL_HPP
