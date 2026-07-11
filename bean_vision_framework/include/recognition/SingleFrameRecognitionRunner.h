#pragma once

#include "recognition/RecognitionRunner.h"

class InputManager;
class BeanNumberDetector;
class RoiParser;

/**
 * @brief 单帧识别执行器。
 *
 * 负责从输入层读取一张图像，执行一次检测和 ROI 解析。
 * 适用于 image/mock 等单帧调试输入。
 */
class SingleFrameRecognitionRunner : public RecognitionRunner {
public:
    /**
     * @brief 构造单帧识别执行器。
     * @param bean_input 豆子阶段使用的输入源。
     * @param digit_input 数字阶段使用的输入源。
     * @param detector 检测器。
     * @param parser ROI 解析器。
     *
     * bean_input 和 digit_input 可以引用同一个 InputManager，
     * 也可以在后续扩展为不同的单帧输入源。
     */
    SingleFrameRecognitionRunner(InputManager& bean_input,
                                 InputManager& digit_input,
                                 BeanNumberDetector& detector,
                                 RoiParser& parser);

    bool scanBeans(VisionResult& result) override;
    bool scanDigits(VisionResult& result) override;
    void reset() override;

private:
    InputManager& bean_input_;
    InputManager& digit_input_;
    BeanNumberDetector& detector_;
    RoiParser& parser_;
};
