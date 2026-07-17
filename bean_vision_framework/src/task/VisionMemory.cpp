#include "task/VisionMemory.h"

namespace {

/**
 * @brief 根据豆子类别得到它要送达的目标数字类别。
 * @param bean_name 归一化后的豆子类别名。
 * @return 目标数字类别名，无法匹配时返回空字符串。
 */
std::string requiredDigit(const std::string& bean_name) {
    if (bean_name == "soybean") {
        return "digit_1";
    }
    if (bean_name == "mung_bean") {
        return "digit_2";
    }
    if (bean_name == "white_kidney_bean") {
        return "digit_3";
    }
    return "";
}

/**
 * @brief 根据取货点识别结果追加一条豆子绑定关系。
 * @param binds 输出绑定关系列表。
 * @param pickup P1/P2/P3 中某个取货点的识别结果。
 */
void appendBind(std::vector<BeanBind>& binds, const PositionResult& pickup) {
    if (!pickup.valid) {
        return;
    }

    BeanBind bind;
    bind.pickup_id = pickup.position_id;
    bind.bean_class = pickup.class_name;
    bind.target_digit = requiredDigit(pickup.class_name);
    bind.confidence = pickup.confidence;
    bind.valid = !bind.target_digit.empty();
    if (bind.valid) {
        binds.push_back(bind);
    }
}

}  // namespace

/**
 * @brief 用当前帧结果更新豆子区缓存。
 * @param current 当前帧 ROI 解析后的视觉结果。
 */
void VisionMemory::updateBeans(const VisionResult& current) {
    bean_binds_.clear();
    if (current.p1.valid) {
        cached_beans_.p1 = current.p1;
    }
    if (current.p2.valid) {
        cached_beans_.p2 = current.p2;
    }
    if (current.p3.valid) {
        cached_beans_.p3 = current.p3;
    }

    // 豆子区缓存更新后，立即重新计算 P 点与目标数字类别的绑定关系。
    appendBind(bean_binds_, cached_beans_.p1);
    appendBind(bean_binds_, cached_beans_.p2);
    appendBind(bean_binds_, cached_beans_.p3);

    cached_beans_.success = beansReady();
    cached_beans_.reason = cached_beans_.success ? "ok" : "bean_position_missing";
}

/**
 * @brief 用当前帧结果更新数字区缓存。
 * @param current 当前帧 ROI 解析后的视觉结果。
 */
void VisionMemory::updateDigits(const VisionResult& current) {
    if (current.l4.valid) {
        cached_digits_.l4 = current.l4;
    }
    if (current.l5.valid) {
        cached_digits_.l5 = current.l5;
    }
    if (current.l6.valid) {
        cached_digits_.l6 = current.l6;
    }
    if (current.l7.valid) {
        cached_digits_.l7 = current.l7;
    }
    if (current.l8.valid) {
        cached_digits_.l8 = current.l8;
    }
    cached_digits_.success = digitsReady();
    cached_digits_.reason = cached_digits_.success ? "ok" : "digit_position_missing";
}

/**
 * @brief 获取豆子与目标数字的绑定关系。
 * @return 豆子绑定关系列表。
 */
const std::vector<BeanBind>& VisionMemory::beanBinds() const {
    return bean_binds_;
}

/**
 * @brief 判断豆子区是否已经缓存到有效结果。
 * @return P1/P2/P3 全部有效时返回 true。
 */
bool VisionMemory::beansReady() const {
    return cached_beans_.p1.valid && cached_beans_.p2.valid && cached_beans_.p3.valid;
}

/**
 * @brief 判断数字区是否已经缓存到有效结果。
 * @return 至少一个 L4-L8 有效时返回 true。
 */
bool VisionMemory::digitsReady() const {
    return cached_digits_.l4.valid || cached_digits_.l5.valid || cached_digits_.l6.valid ||
           cached_digits_.l7.valid || cached_digits_.l8.valid;
}

/**
 * @brief 判断缓存信息是否足够尝试生成任务。
 * @return 豆子区和数字区都已有有效结果时返回 true。
 */
bool VisionMemory::readyForTask() const {
    return beansReady() && digitsReady();
}

/**
 * @brief 返回合并后的视觉结果。
 * @return 可继续交给 TaskGenerator 的 VisionResult。
 */
VisionResult VisionMemory::mergedResult() const {
    VisionResult result;
    result.p1 = cached_beans_.p1;
    result.p2 = cached_beans_.p2;
    result.p3 = cached_beans_.p3;
    result.l4 = cached_digits_.l4;
    result.l5 = cached_digits_.l5;
    result.l6 = cached_digits_.l6;
    result.l7 = cached_digits_.l7;
    result.l8 = cached_digits_.l8;
    result.success = readyForTask();
    result.reason = result.success ? "ok" : "memory_not_ready";
    return result;
}

/**
 * @brief 清空缓存，准备下一轮任务。
 */
void VisionMemory::clear() {
    cached_beans_ = VisionResult{};
    cached_digits_ = VisionResult{};
    bean_binds_.clear();
}
