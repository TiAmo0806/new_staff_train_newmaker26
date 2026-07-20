#include "detector.hpp"
#include <algorithm>
#include <cstring>
#include <stdio.h>

// 默认 8 个豆类类别（与 metadata.yaml 一致）
static const std::vector<std::string> DEFAULT_CLASS_NAMES = {
    "soybean", "mung_bean", "white_kidney_bean",
    "data_1", "data_2", "data_3", "data_4", "data_5"
};

// ============================================================
// Detector::load() — 加载模型 + PrePostProcessor + 预热
// ============================================================
bool Detector::load(const std::string& modelPath, const std::string& device) {
    printf("[Detector] 加载模型: %s\n", modelPath.c_str());

    try {
        auto model = m_core.read_model(modelPath);

        // ============================================================
        // PrePostProcessor — 将预处理集成到模型图中
        //
        // 之前的数据流（CPU 端，慢）：
        //   BGR uint8 → cvtColor(BGR→RGB) → resize(640) →
        //   convertTo(float32) → /255.0 → 三层 for 循环 HWC→CHW
        //
        // 现在的数据流（OpenVINO 图中，快）：
        //   BGR uint8 → cv::resize(640) → memcpy → PPP(图中完成所有转换)
        //
        // PPP 内部处理: BGR→RGB, uint8→f32, /255.0, NHWC→NCHW
        // ============================================================
        ov::preprocess::PrePostProcessor ppp(model);
        auto& input = ppp.input();

        // 告诉模型：实际输入是 uint8 BGR NHWC 格式
        input.tensor()
            .set_element_type(ov::element::u8)
            .set_shape({1,
                        static_cast<size_t>(inputHeight),
                        static_cast<size_t>(inputWidth), 3})
            .set_layout("NHWC")
            .set_color_format(ov::preprocess::ColorFormat::BGR);

        // 模型内部期望 NCHW 布局（YOLO 标准输入）
        input.model().set_layout("NCHW");

        // 图中完成的预处理步骤
        input.preprocess()
            .convert_element_type(ov::element::f32)        // uint8 → float32
            .convert_color(ov::preprocess::ColorFormat::RGB) // BGR → RGB
            .scale({255.f, 255.f, 255.f});                 // /255.0 归一化到 [0,1]

        model = ppp.build();

        // 编译到目标设备
        m_compiledModel = m_core.compile_model(model, device);

        // 创建 2 个推理请求（异步流水线）
        m_reqA = m_compiledModel.create_infer_request();
        m_reqB = m_compiledModel.create_infer_request();

        // 预分配 resize 缓冲区（复用，避免每帧分配）
        m_resizedBuf = cv::Mat(inputHeight, inputWidth, CV_8UC3);

        m_classNames = DEFAULT_CLASS_NAMES;
        m_loaded = true;

        printModelInfo();

        // ---- 模型预热（消除首帧冷启动 ~100ms+ 延迟） ----
        printf("[Detector] 模型预热中...\n");
        ov::Shape warmupShape = {1,
                                 static_cast<size_t>(inputHeight),
                                 static_cast<size_t>(inputWidth), 3};
        ov::Tensor warmupTensor(ov::element::u8, warmupShape);
        std::memset(warmupTensor.data(), 128, warmupTensor.get_byte_size());

        // 两路请求都预热
        m_reqA.set_input_tensor(warmupTensor);
        m_reqA.infer();
        m_reqB.set_input_tensor(warmupTensor);
        m_reqB.infer();
        printf("[Detector] 预热完成\n");

    } catch (const std::exception& e) {
        fprintf(stderr, "[Detector] ERROR: %s\n", e.what());
        return false;
    }

    return true;
}

// ============================================================
// Detector::detect() — 异步流水线推理
//
// 流水线示意（2 路交替）：
//   Frame 0: [resize+copy] → start_async(A)
//   Frame 1: [resize+copy] → start_async(B) | wait(A) → postprocess → return
//   Frame 2: [resize+copy] → start_async(A) | wait(B) → postprocess → return
//   ...
//
// 好处：CPU 预处理与 GPU/CPU 推理重叠执行
// ============================================================
std::vector<Detection> Detector::detect(const cv::Mat& frame) {
    if (!m_loaded) return {};

    // ---- 1. Resize 到模型输入尺寸（cv::resize 是 SIMD 优化的，很快） ----
    cv::resize(frame, m_resizedBuf, cv::Size(inputWidth, inputHeight));

    // ---- 2. 封装为 tensor（拷贝数据以保证异步安全） ----
    //        单次 memcpy ~1.2MB，在树莓派上 <1ms
    ov::Shape inputShape = {1,
                            static_cast<size_t>(inputHeight),
                            static_cast<size_t>(inputWidth), 3};
    ov::Tensor inputTensor(ov::element::u8, inputShape);
    std::memcpy(inputTensor.data<uint8_t>(),
                m_resizedBuf.data,
                inputTensor.get_byte_size());

    // ---- 3. 在当前请求上启动异步推理 ----
    ov::InferRequest& currentReq = (m_activeReq == 0) ? m_reqA : m_reqB;
    currentReq.set_input_tensor(inputTensor);
    currentReq.start_async();

    // ---- 4. 等待上一帧的推理结果 ----
    std::vector<Detection> results;
    if (m_hasPending) {
        ov::InferRequest& prevReq = (m_activeReq == 0) ? m_reqB : m_reqA;
        prevReq.wait();
        auto outputTensor = prevReq.get_output_tensor();
        results = postprocess(outputTensor,
                              m_origW[1 - m_activeReq],
                              m_origH[1 - m_activeReq]);
    }

    // ---- 5. 保存当前帧的原始尺寸（供下一轮后处理缩放用） ----
    m_origW[m_activeReq] = frame.cols;
    m_origH[m_activeReq] = frame.rows;

    // ---- 6. 切换活跃请求 ----
    m_activeReq  = 1 - m_activeReq;
    m_hasPending = true;

    return results;
}

// ============================================================
// Detector::waitAll() — 程序退出前等待所有推理完成
// ============================================================
void Detector::waitAll() {
    if (!m_loaded) return;
    try {
        m_reqA.wait();
        m_reqB.wait();
    } catch (...) {}
    m_hasPending = false;
}

// ============================================================
// computeIoU — 计算两个检测框的 IoU（交并比）
// ============================================================
static float computeIoU(const Detection& a, const Detection& b) {
    float interX1 = std::max(a.x1, b.x1);
    float interY1 = std::max(a.y1, b.y1);
    float interX2 = std::min(a.x2, b.x2);
    float interY2 = std::min(a.y2, b.y2);

    float interW = std::max(0.0f, interX2 - interX1);
    float interH = std::max(0.0f, interY2 - interY1);
    float interArea = interW * interH;

    float areaA = (a.x2 - a.x1) * (a.y2 - a.y1);
    float areaB = (b.x2 - b.x1) * (b.y2 - b.y1);
    float unionArea = areaA + areaB - interArea;

    return (unionArea > 0.0f) ? interArea / unionArea : 0.0f;
}

// ============================================================
// Detector::postprocess() —— 解析输出 + NMS
// ============================================================
std::vector<Detection> Detector::postprocess(const ov::Tensor& output,
                                               int origW, int origH) {
    std::vector<Detection> detections;

    const float* outData = output.data<float>();
    ov::Shape outShape = output.get_shape();

    // 期望 [1, 4+num_classes, num_anchors]
    if (outShape.size() != 3) {
        fprintf(stderr, "[Detector] ERROR: 输出维度=%zu（期望3维）\n",
                outShape.size());
        return detections;
    }

    size_t numPred  = outShape[1];  // 4 + num_classes
    size_t numBoxes = outShape[2];  // num_anchors
    size_t numCls   = numPred - 4;

    float scaleX = static_cast<float>(origW) / inputWidth;
    float scaleY = static_cast<float>(origH) / inputHeight;

    // ---- 筛选高置信度候选框 ----
    std::vector<Detection> candidates;
    for (size_t i = 0; i < numBoxes; ++i) {
        // 找最高分类置信度
        float maxConf = 0.0f;
        int   bestCls = -1;
        for (size_t c = 0; c < numCls; ++c) {
            float score = outData[(4 + c) * numBoxes + i];
            if (score > maxConf) { maxConf = score; bestCls = static_cast<int>(c); }
        }
        if (maxConf < confThreshold) continue;

        // 解析 cx,cy,w,h
        float cx = outData[0 * numBoxes + i];
        float cy = outData[1 * numBoxes + i];
        float w  = outData[2 * numBoxes + i];
        float h  = outData[3 * numBoxes + i];

        // 中心点 → 左上右下，缩放到原图坐标
        float x1 = (cx - w / 2.0f) * scaleX;
        float y1 = (cy - h / 2.0f) * scaleY;
        float x2 = (cx + w / 2.0f) * scaleX;
        float y2 = (cy + h / 2.0f) * scaleY;

        // 边界裁剪
        x1 = std::max(0.0f, std::min(x1, static_cast<float>(origW)));
        y1 = std::max(0.0f, std::min(y1, static_cast<float>(origH)));
        x2 = std::max(0.0f, std::min(x2, static_cast<float>(origW)));
        y2 = std::max(0.0f, std::min(y2, static_cast<float>(origH)));
        if (x2 <= x1 || y2 <= y1) continue;

        Detection det;
        det.x1 = x1;  det.y1 = y1;
        det.x2 = x2;  det.y2 = y2;
        det.confidence = maxConf;
        det.class_id   = bestCls;
        det.class_name = (bestCls < static_cast<int>(m_classNames.size()))
                             ? m_classNames[bestCls] : "unknown";
        candidates.push_back(det);
    }

    // ---- NMS（按置信度降序，贪心保留） ----
    std::sort(candidates.begin(), candidates.end(),
              [](const Detection& a, const Detection& b) {
                  return a.confidence > b.confidence;
              });

    for (const auto& candidate : candidates) {
        bool suppressed = false;
        for (const auto& kept : detections) {
            if (computeIoU(candidate, kept) > nmsThreshold) {
                suppressed = true;
                break;
            }
        }
        if (!suppressed) {
            detections.push_back(candidate);
        }
    }

    return detections;
}

// ============================================================
// Detector::printModelInfo() —— 打印模型输入/输出
// ============================================================
void Detector::printModelInfo() const {
    auto inputs  = m_compiledModel.inputs();
    auto outputs = m_compiledModel.outputs();

    printf("\n===== 模型信息 =====\n");
    for (const auto& in : inputs) {
        auto names = in.get_names();
        printf("输入: %s, 形状: ",
               names.empty() ? "?" : names.begin()->c_str());
        for (auto dim : in.get_partial_shape()) {
            if (dim.is_dynamic()) printf("? ");
            else printf("%ld ", dim.get_length());
        }
        printf("\n");
    }
    for (const auto& out : outputs) {
        auto names = out.get_names();
        printf("输出: %s, 形状: ",
               names.empty() ? "?" : names.begin()->c_str());
        for (auto dim : out.get_partial_shape()) {
            if (dim.is_dynamic()) printf("? ");
            else printf("%ld ", dim.get_length());
        }
        printf("\n");
    }
    printf("=====================\n\n");
}
