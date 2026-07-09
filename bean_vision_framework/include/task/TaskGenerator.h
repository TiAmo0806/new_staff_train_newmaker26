#pragma once

#include "core/TaskTypes.h"
#include "core/VisionResult.h"

class TaskGenerator {
public:
    /**
     * @brief 根据视觉位置结果生成搬运任务。
     * @param vision ROI 解析后的视觉结果。
     * @return 任务生成结果，成功时包含 3 条搬运任务。
     *
     * 规则：soybean->digit_1，mung_bean->digit_2，
     * white_kidney_bean->digit_3。
     */
    TaskResult generate(const VisionResult& vision);
};
