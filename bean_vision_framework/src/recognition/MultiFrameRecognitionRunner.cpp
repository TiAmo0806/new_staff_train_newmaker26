#include "recognition/MultiFrameRecognitionRunner.h"

#include "core/AppConfig.h"
#include "detector/BeanNumberDetector.h"
#include "input/InputManager.h"
#include "parser/RoiParser.h"

MultiFrameRecognitionRunner::MultiFrameRecognitionRunner(const AppConfig& config,
                                                         InputManager& input,
                                                         BeanNumberDetector& detector,
                                                         RoiParser& parser)
    : input_(input),
      detector_(detector),
      parser_(parser),
      recognizer_(config) {}

bool MultiFrameRecognitionRunner::scanBeans(VisionResult& result) {
    result = recognizer_.scanBeans(input_, detector_, parser_);
    return result.success;
}

bool MultiFrameRecognitionRunner::scanDigits(VisionResult& result) {
    result = recognizer_.scanDigits(input_, detector_, parser_);
    return result.success;
}

void MultiFrameRecognitionRunner::reset() {
    // 现有 MultiFrameRecognizer 只保存只读配置，没有跨轮缓存状态。
    // 因此 reset 只需要把输入源读指针恢复到下一轮可用状态。
    input_.reset();
}
