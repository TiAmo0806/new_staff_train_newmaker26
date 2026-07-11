#include "recognition/SingleFrameRecognitionRunner.h"

#include "detector/BeanNumberDetector.h"
#include "input/InputManager.h"
#include "parser/RoiParser.h"

namespace {

bool scanSingleFrame(InputManager& input,
                     BeanNumberDetector& detector,
                     RoiParser& parser,
                     VisionResult& result) {
    input.reset();

    cv::Mat frame;
    if (!input.read(frame) || frame.empty()) {
        result = VisionResult{};
        result.success = false;
        result.reason = "input_read_failed";
        return false;
    }

    const std::vector<Detection> detections = detector.detect(frame);
    result = parser.parse(detections);
    return result.success;
}

}  // namespace

SingleFrameRecognitionRunner::SingleFrameRecognitionRunner(InputManager& bean_input,
                                                           InputManager& digit_input,
                                                           BeanNumberDetector& detector,
                                                           RoiParser& parser)
    : bean_input_(bean_input),
      digit_input_(digit_input),
      detector_(detector),
      parser_(parser) {}

bool SingleFrameRecognitionRunner::scanBeans(VisionResult& result) {
    return scanSingleFrame(bean_input_, detector_, parser_, result);
}

bool SingleFrameRecognitionRunner::scanDigits(VisionResult& result) {
    return scanSingleFrame(digit_input_, detector_, parser_, result);
}

void SingleFrameRecognitionRunner::reset() {
    bean_input_.reset();
    digit_input_.reset();
}
