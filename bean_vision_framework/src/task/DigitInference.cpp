#include "task/DigitInference.h"

#include <array>

namespace {

int digitValue(const PositionResult& position) {
    if (!position.valid) {
        return 0;
    }
    if (position.class_name == "digit_1") {
        return 1;
    }
    if (position.class_name == "digit_2") {
        return 2;
    }
    if (position.class_name == "digit_3") {
        return 3;
    }
    if (position.class_name == "digit_4") {
        return 4;
    }
    if (position.class_name == "digit_5") {
        return 5;
    }
    return 0;
}

}  // namespace

DigitInferenceResult DigitInference::analyze(const VisionResult& result) const {
    DigitInferenceResult inference;
    inference.success = true;

    const std::array<std::pair<int, const PositionResult*>, 5> places = {{
        {4, &result.l4},
        {5, &result.l5},
        {6, &result.l6},
        {7, &result.l7},
        {8, &result.l8},
    }};

    for (const auto& [place_id, position] : places) {
        const int digit = digitValue(*position);
        if (digit > 0) {
            inference.place_to_digit[place_id] = digit;
        } else {
            inference.missing_places.push_back(place_id);
        }
    }

    inference.complete = inference.missing_places.empty();
    inference.reliable = inference.complete;
    inference.reason = inference.complete ? "ok" : "digit_incomplete";
    return inference;
}
