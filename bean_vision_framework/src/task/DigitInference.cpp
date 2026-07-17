#include "task/DigitInference.h"

#include <array>
#include <set>

namespace {

struct DigitSetAnalysis {
    bool all_in_range = true;
    bool unique = true;
    std::set<int> seen_digits;
};

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

DigitSetAnalysis analyzeDigitSet(const std::map<int, int>& place_to_digit) {
    DigitSetAnalysis analysis;
    for (const auto& [place_id, digit] : place_to_digit) {
        (void)place_id;
        if (digit < 1 || digit > 5) {
            analysis.all_in_range = false;
        }
        if (!analysis.seen_digits.insert(digit).second) {
            analysis.unique = false;
        }
    }
    return analysis;
}

bool isCompleteDigitPermutation(const std::map<int, int>& place_to_digit) {
    if (place_to_digit.size() != 5) {
        return false;
    }

    const DigitSetAnalysis analysis = analyzeDigitSet(place_to_digit);
    return analysis.all_in_range &&
           analysis.unique &&
           analysis.seen_digits.size() == 5 &&
           analysis.seen_digits == std::set<int>({1, 2, 3, 4, 5});
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

    if (inference.missing_places.empty() && isCompleteDigitPermutation(inference.place_to_digit)) {
        inference.complete = true;
        inference.reliable = true;
        inference.reason = "ok";
        return inference;
    }

    if (inference.missing_places.size() == 1 && inference.place_to_digit.size() == 4) {
        const DigitSetAnalysis analysis = analyzeDigitSet(inference.place_to_digit);

        if (analysis.all_in_range && analysis.unique) {
            std::vector<int> missing_digits;
            for (int digit = 1; digit <= 5; ++digit) {
                if (analysis.seen_digits.count(digit) == 0) {
                    missing_digits.push_back(digit);
                }
            }
            if (missing_digits.size() == 1) {
                const int inferred_place = inference.missing_places.front();
                const int inferred_digit = missing_digits.front();
                inference.place_to_digit[inferred_place] = inferred_digit;
                inference.inferred = true;
                inference.inferred_places.push_back(inferred_place);
                inference.missing_places.clear();
                inference.complete = true;
                inference.reliable = true;
                inference.reason = "inferred_missing_digit";
                return inference;
            }
        }
    }

    inference.complete = false;
    inference.reliable = false;
    inference.inferred = false;
    inference.reason = "digit_incomplete";
    return inference;
}
