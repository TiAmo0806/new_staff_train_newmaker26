#include "task/BeanInference.h"

#include <array>
#include <set>
#include <vector>

namespace {

const std::set<std::string> kBeanClasses = {
    "soybean",
    "mung_bean",
    "white_kidney_bean",
};

bool isKnownBean(const PositionResult& position) {
    return kBeanClasses.count(position.class_name) != 0;
}

int beanClassId(const std::string& class_name) {
    if (class_name == "soybean") {
        return 0;
    }
    if (class_name == "mung_bean") {
        return 1;
    }
    if (class_name == "white_kidney_bean") {
        return 2;
    }
    return -1;
}

}  // namespace

BeanInferenceResult BeanInference::analyze(const VisionResult& result) const {
    BeanInferenceResult inference;
    inference.completed_result = result;
    inference.completed_result.success = false;

    const std::array<std::pair<const char*, const PositionResult*>, 3> positions = {{
        {"P1", &result.p1},
        {"P2", &result.p2},
        {"P3", &result.p3},
    }};

    std::vector<std::pair<const char*, const PositionResult*>> valid_positions;
    for (const auto& position : positions) {
        if (position.second->valid) {
            valid_positions.push_back(position);
            if (!isKnownBean(*position.second)) {
                inference.reason = "unknown_bean_class";
                inference.completed_result.reason = inference.reason;
                return inference;
            }
        }
    }

    if (valid_positions.size() <= 1) {
        inference.reason = "insufficient_beans";
        inference.completed_result.reason = inference.reason;
        return inference;
    }

    std::set<std::string> recognized_classes;
    for (const auto& position : valid_positions) {
        recognized_classes.insert(position.second->class_name);
    }
    if (recognized_classes.size() != valid_positions.size()) {
        inference.reason = "bean_class_conflict";
        inference.completed_result.reason = inference.reason;
        return inference;
    }

    if (valid_positions.size() == 3) {
        // 三个已知且不同的类别必然正好是固定集合的一个排列。
        inference.complete = true;
        inference.reliable = true;
        inference.reason = "ok";
        inference.completed_result.success = true;
        inference.completed_result.reason = inference.reason;
        return inference;
    }

    // 此处恰好有两个有效、已知且不同的 P 位置；补出唯一剩余类别。
    const char* missing_position = nullptr;
    PositionResult* completed_position = nullptr;
    if (!inference.completed_result.p1.valid) {
        missing_position = "P1";
        completed_position = &inference.completed_result.p1;
    } else if (!inference.completed_result.p2.valid) {
        missing_position = "P2";
        completed_position = &inference.completed_result.p2;
    } else {
        missing_position = "P3";
        completed_position = &inference.completed_result.p3;
    }

    std::string missing_class;
    for (const std::string& bean_class : kBeanClasses) {
        if (recognized_classes.count(bean_class) == 0) {
            missing_class = bean_class;
            break;
        }
    }

    completed_position->valid = true;
    completed_position->position_id = missing_position;
    completed_position->class_name = missing_class;
    completed_position->class_id = beanClassId(missing_class);
    completed_position->confidence = 0.0f;
    completed_position->box = cv::Rect{};
    completed_position->center_px = cv::Point{};
    completed_position->offset_px = cv::Point{};

    inference.complete = true;
    inference.reliable = true;
    inference.inferred = true;
    inference.reason = "inferred_missing_bean";
    inference.inferred_position = missing_position;
    inference.inferred_class = missing_class;
    inference.completed_result.success = true;
    inference.completed_result.reason = inference.reason;
    return inference;
}
