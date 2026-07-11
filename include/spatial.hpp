#ifndef SPATIAL_HPP
#define SPATIAL_HPP

#include "detector.hpp"   // Detection 结构体

#include <vector>
#include <string>
#include <opencv2/opencv.hpp>

/**
 * 检测对象的中心坐标信息（从 Detection 扩展）
 */
struct ObjectCenter {
    int    class_id;       // 类别 ID
    std::string class_name; // 类别名称
    float  x1, y1, x2, y2; // 4 个角坐标
    float  cx, cy;         // 中心坐标

    /// 从 Detection 构造，自动计算中心点
    static ObjectCenter fromDetection(const Detection& d);
};

/**
 * 两个对象之间的相对位置关系
 */
struct SpatialRelation {
    std::string objectA;    // 对象 A 名称
    std::string objectB;    // 对象 B 名称
    std::string xRelation;  // X 方向关系: "左边" / "右边" / "同一列"
    std::string yRelation;  // Y 方向关系: "前面" / "后面" / "同一行"
};

/**
 * 空间位置分析器
 *
 * 相机视角约定（俯拍/斜拍桌面）：
 *   - Y 轴：画面下方 = 离相机近 = "前面"
 *           画面上方 = 离相机远 = "后面"
 *   - X 轴：画面左边 = "左边"，画面右边 = "右边"
 */
class SpatialAnalyzer {
public:
    /// 允许偏差（像素），小于此值视为"同行"或"同列"
    float xTolerance = 50.0f;
    float yTolerance = 50.0f;

    /**
     * 分析所有检测对象之间的相对位置关系
     *
     * @param detections  模型检测结果
     * @return            所有对象两两之间的位置关系
     */
    std::vector<SpatialRelation> analyze(
        const std::vector<Detection>& detections) const;

    /**
     * 获取所有对象的中心点信息（含 4 角坐标）
     */
    std::vector<ObjectCenter> extractCenters(
        const std::vector<Detection>& detections) const;

    /**
     * 将一组位置关系格式化为可读文字
     * 例如: "soybean 在 mung_bean 的左边，后面"
     */
    static std::string format(const SpatialRelation& rel);
};

/**
 * 在图像上绘制中心点和位置关系连线（可选）
 *
 * @param frame       被绘制的图像
 * @param centers     对象中心点集合
 * @param relations   位置关系集合（为空则不画连线）
 */
void drawCenters(cv::Mat& frame,
                 const std::vector<ObjectCenter>& centers,
                 const std::vector<SpatialRelation>& relations = {});

#endif // SPATIAL_HPP
