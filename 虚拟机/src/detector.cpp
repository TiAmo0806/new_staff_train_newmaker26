#include "/home/hu/mvs_openvino_demo/include/detector.hpp"
#include "preprocess.hpp"
#include <algorithm>
#include <stdio.h>

// 默认 8 个豆类类别（与 best_openvino_model/metadata.yaml 一致）
static const std::vector<std::string> DEFAULT_CLASS_NAMES = {
    "soybean", "mung_bean", "white_kidney_bean",
    "data_1", "data_2", "data_3", "data_4", "data_5"
};

// ============================================================
// Detector::load() —— 加载 OpenVINO IR 模型
// ============================================================
bool Detector::load(const std::string& modelPath, const std::string& device) {
    printf("[Detector] 加载模型: %s\n", modelPath.c_str());

    try {
        // 1. 读取 IR 模型（.xml, .bin 自动配对）
        auto model = m_core.read_model(modelPath);

        // 2. 编译到设备，"AUTO" = 自动选最优
        m_compiledModel = m_core.compile_model(model, device);

        // 3. 创建推理请求（复用，避免重复创建）
        m_inferRequest = m_compiledModel.create_infer_request();

        m_classNames = DEFAULT_CLASS_NAMES;
        m_loaded = true;

        printModelInfo();
    } catch (const std::exception& e) {
        fprintf(stderr, "[Detector] ERROR: %s\n", e.what());
        return false;
    }

    return true;
}

// ============================================================
// Detector::detect() —— 完整推理管线
// ============================================================
std::vector<Detection> Detector::detect(const cv::Mat& frame) {
    if (!m_loaded) return {};

    // 1. 预处理
    cv::Mat blob = preprocess(frame);

    // 2. HWC → CHW，封装 Tensor
    ov::Tensor inputTensor = blob_to_tensor(blob);

    // 3. 推理
    m_inferRequest.set_input_tensor(inputTensor);
    m_inferRequest.infer();
    auto outputTensor = m_inferRequest.get_output_tensor();

    // 4. 后处理
    return postprocess(outputTensor, frame.cols, frame.rows);
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

    std::vector<bool> removed(candidates.size(), false);
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (removed[i]) continue;
        detections.push_back(candidates[i]);

        for (size_t j = i + 1; j < candidates.size(); ++j) {
            if (removed[j]) continue;

            float interX1 = std::max(candidates[i].x1, candidates[j].x1);
            float interY1 = std::max(candidates[i].y1, candidates[j].y1);
            float interX2 = std::min(candidates[i].x2, candidates[j].x2);
            float interY2 = std::min(candidates[i].y2, candidates[j].y2);

            float interArea = std::max(0.0f, interX2 - interX1) *
                              std::max(0.0f, interY2 - interY1);
            float areaI = (candidates[i].x2 - candidates[i].x1) *
                          (candidates[i].y2 - candidates[i].y1);
            float areaJ = (candidates[j].x2 - candidates[j].x1) *
                          (candidates[j].y2 - candidates[j].y1);
            float unionArea = areaI + areaJ - interArea;

            if (unionArea > 0.0f && interArea / unionArea > nmsThreshold) {
                removed[j] = true;
            }
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
