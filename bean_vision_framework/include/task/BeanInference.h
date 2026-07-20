#pragma once

#include "core/VisionResult.h"

#include <string>

// 豆子区完整性检查与“两豆推三豆”的规则分析结果。
struct BeanInferenceResult {
    bool complete = false;
    bool reliable = false;
    bool inferred = false;
    std::string reason;

    VisionResult completed_result;

    std::string inferred_position;
    std::string inferred_class;
};

class BeanInference {
public:
    /**
     * @brief 验证 P1/P2/P3 的豆子排列；仅在两个已知且不同的类别时补全缺失位置。
     */
    BeanInferenceResult analyze(const VisionResult& result) const;
};
