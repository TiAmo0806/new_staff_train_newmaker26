#pragma once

#include "core/VisionResult.h"

#include <map>
#include <string>
#include <vector>

struct DigitInferenceResult {
    bool success = false;              // 推断流程是否正常完成。
    bool complete = false;             // L4-L8 是否全部识别到有效数字。
    bool reliable = false;             // 第一版中 complete 即 reliable。
    bool inferred = false;             // 是否存在基于规则补全出的数字位置。
    std::string reason;                // ok / digit_incomplete 等原因。
    std::map<int, int> place_to_digit; // 4->1, 5->2 ...
    std::vector<int> missing_places;   // 例如 {5, 7}。
    std::vector<int> inferred_places;  // 例如 {7}，表示该位置数字由规则补全得到。
};

class DigitInference {
public:
    /**
     * @brief 对数字区识别结果做第一版完整性判断。
     * @param result RecognitionRunner 输出的数字区 VisionResult。
     * @return 数字完整性与位置映射分析结果。
     */
    DigitInferenceResult analyze(const VisionResult& result) const;
};
