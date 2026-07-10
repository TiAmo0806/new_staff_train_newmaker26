#ifndef DETECTOR_HPP
#define DETECTOR_HPP

#include <opencv2/opencv.hpp>
#include <openvino/openvino.hpp>

#include <vector>
#include <string>

/**
 * 单个检测结果
 */
struct Detection {
    float x1, y1, x2, y2;    // 边界框（像素坐标）
    float confidence;         // 置信度 [0, 1]
    int   class_id;           // 类别 ID
    std::string class_name;   // 类别名称
};

/**
 * YOLO11 目标检测器（OpenVINO 后端）
 *
 * 使用方式：
 *   Detector det;
 *   det.load("best.xml");
 *   auto results = det.detect(frame);
 *   // 遍历 results 绘制检测框
 */
class Detector {
public:
    /// 可调参数
    float confThreshold = 0.4f;   // 置信度阈值
    float nmsThreshold  = 0.5f;   // NMS IoU 阈值
    int   inputWidth    = 640;    // 模型输入宽度
    int   inputHeight   = 640;    // 模型输入高度

    Detector() = default;

    /**
     * 加载 OpenVINO IR 模型
     * @param modelPath  .xml 模型文件路径（.bin 需在同目录）
     * @param device     目标设备，默认 "AUTO"（自动选择 CPU/GPU）
     * @return 加载成功返回 true
     */
    bool load(const std::string& modelPath, const std::string& device = "AUTO");

    /**
     * 执行目标检测
     * @param frame  输入图像（BGR, uint8, 任意分辨率）
     * @return       检测结果列表
     */
    std::vector<Detection> detect(const cv::Mat& frame);

    /// 打印模型输入/输出信息（调试用）
    void printModelInfo() const;

    /// 获取类别名称列表
    const std::vector<std::string>& classNames() const { return m_classNames; }

private:
    /// 后处理：解析输出张量 → NMS
    std::vector<Detection> postprocess(const ov::Tensor& output,
                                        int origW, int origH);

    ov::Core              m_core;
    ov::CompiledModel     m_compiledModel;
    ov::InferRequest      m_inferRequest;
    std::vector<std::string> m_classNames;
    bool m_loaded = false;
};

#endif // DETECTOR_HPP
