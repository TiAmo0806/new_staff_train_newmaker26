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
 * 性能优化：
 *   - PrePostProcessor 将预处理集成到模型图中，消除 CPU 端 HWC→CHW 转换
 *   - 双 InferRequest 异步流水线，隐藏推理延迟
 *   - 预分配缓冲区，避免每帧内存分配
 *   - 首次加载时自动预热，消除冷启动延迟
 *
 * 使用方式：
 *   Detector det;
 *   det.load("best.xml");
 *   auto results = det.detect(frame);  // frame 为 BGR, uint8, 任意分辨率
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
     * 加载 OpenVINO IR 模型并配置 PrePostProcessor
     *
     * PPP 将 BGR→RGB、uint8→f32、归一化、NHWC→NCHW 全部
     * 集成到模型图中，detect() 只需传入原始 BGR cv::Mat。
     *
     * @param modelPath  .xml 模型文件路径
     * @param device     目标设备，默认 "AUTO"
     * @return 加载成功返回 true
     */
    bool load(const std::string& modelPath, const std::string& device = "AUTO");

    /**
     * 执行目标检测（异步流水线）
     *
     * 每帧调用时启动异步推理，同时等待上一帧的结果。
     * - 首帧返回空（预热），第二帧开始正常返回结果
     * - frame 为 BGR uint8，任意分辨率均可
     *
     * @param frame  BGR 图像（uint8, 任意分辨率）
     * @return       上一帧的检测结果（首帧为空）
     */
    std::vector<Detection> detect(const cv::Mat& frame);

    /// 等待所有进行中的异步推理完成（程序退出前调用）
    void waitAll();

    /// 打印模型输入/输出信息（调试用）
    void printModelInfo() const;

    /// 获取类别名称列表
    const std::vector<std::string>& classNames() const { return m_classNames; }

private:
    /// 后处理：解析输出张量 → NMS
    std::vector<Detection> postprocess(const ov::Tensor& output,
                                        int origW, int origH);

    ov::Core                 m_core;
    ov::CompiledModel        m_compiledModel;
    ov::InferRequest         m_reqA;           // 异步请求 A
    ov::InferRequest         m_reqB;           // 异步请求 B
    std::vector<std::string> m_classNames;

    // 预分配缓冲区（避免每帧 new/delete）
    cv::Mat  m_resizedBuf;       // 640×640×3 uint8（复用）

    // 异步流水线状态
    int   m_activeReq  = 0;      // 当前活跃请求: 0=A, 1=B
    bool  m_hasPending = false;  // 是否有正在进行的异步推理
    int   m_origW[2]   = {0, 0};// 每路请求对应的原始帧宽
    int   m_origH[2]   = {0, 0};// 每路请求对应的原始帧高

    bool m_loaded = false;
};

#endif // DETECTOR_HPP
