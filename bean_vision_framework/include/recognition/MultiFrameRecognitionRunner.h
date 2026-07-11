#pragma once

#include "recognition/RecognitionRunner.h"
#include "recognition/MultiFrameRecognizer.h"

class InputManager;
class BeanNumberDetector;
class RoiParser;
struct AppConfig;

/**
 * @brief 多帧识别执行器。
 *
 * 该类只负责把 RecognitionRunner 接口适配到现有
 * MultiFrameRecognizer，不复制现有投票与重试逻辑。
 */
class MultiFrameRecognitionRunner : public RecognitionRunner {
public:
    /**
     * @brief 构造多帧识别执行器。
     * @param config 总配置，传递给现有 MultiFrameRecognizer。
     * @param input 多帧读取使用的输入源。
     * @param detector 检测器。
     * @param parser ROI 解析器。
     */
    MultiFrameRecognitionRunner(const AppConfig& config,
                                InputManager& input,
                                BeanNumberDetector& detector,
                                RoiParser& parser);

    bool scanBeans(VisionResult& result) override;
    bool scanDigits(VisionResult& result) override;
    void reset() override;

private:
    InputManager& input_;
    BeanNumberDetector& detector_;
    RoiParser& parser_;
    MultiFrameRecognizer recognizer_;
};
