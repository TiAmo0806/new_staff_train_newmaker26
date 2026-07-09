#pragma once

#include "core/AppConfig.h"
#include "core/VisionResult.h"
#include "detector/BeanNumberDetector.h"
#include "input/InputManager.h"
#include "parser/RoiParser.h"

/**
 * @brief 多帧稳定识别器。
 *
 * 负责从已打开的输入源连续读取多帧，逐帧检测和 ROI 解析，再按固定位置投票融合。
 */
class MultiFrameRecognizer {
public:
    /**
     * @brief 构造多帧识别器。
     * @param config 应用配置，读取 scan/debug/roi 配置。
     */
    explicit MultiFrameRecognizer(const AppConfig& config);

    /**
     * @brief 扫描豆子区 P1/P2/P3。
     */
    VisionResult scanBeans(InputManager& input, BeanNumberDetector& detector, RoiParser& parser);

    /**
     * @brief 扫描数字区 L4-L8。
     */
    VisionResult scanDigits(InputManager& input, BeanNumberDetector& detector, RoiParser& parser);

private:
    VisionResult scan(InputManager& input, BeanNumberDetector& detector, RoiParser& parser, bool scan_beans);

    const AppConfig& config_;
};
