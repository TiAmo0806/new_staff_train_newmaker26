#include "command/SerialCommandSource.h"
#include "command/TerminalCommandSource.h"
#include "communication/ByteConverter.h"
#include "communication/Protocol.h"
#include "communication/SerialPort.h"
#include "core/AppConfig.h"
#include "detector/BeanNumberDetector.h"
#include "input/InputManager.h"
#include "parser/RoiParser.h"
#include "recognition/MultiFrameRecognitionRunner.h"
#include "recognition/MultiFrameRecognizer.h"
#include "recognition/RecognitionRunner.h"
#include "recognition/SingleFrameRecognitionRunner.h"
#include "task/TaskGenerator.h"
#include "task/TaskStateMachine.h"
#include "utils/DebugLogger.h"
#include "utils/DrawUtils.h"
#include "utils/LogUtils.h"

#include <opencv2/highgui.hpp>

#include <exception>
#include <iostream>
#include <memory>

namespace {

void printMainUsage(const char* app) {
    std::cout << "Usage:\n";
    std::cout << "  " << app << " <config_path>\n";
    std::cout << "  " << app << " --help\n";
    std::cout << "\n";
    std::cout << "Examples:\n";
    std::cout << "  " << app << " ../config/debug_command_image.yaml\n";
    std::cout << "  " << app << " ../config/debug_camera_mock.yaml\n";
    std::cout << "  " << app << " ../config/debug_image_real_serial.yaml\n";
    std::cout << "  " << app << " ../config/real_robot.yaml\n";
    std::cout << "\n";
    std::cout << "Supported runtime configs:\n";
    std::cout << "  debug_command_image     image input + terminal command + mock serial\n";
    std::cout << "  debug_camera_mock       industrial camera + terminal command + mock serial\n";
    std::cout << "  debug_image_real_serial image input + terminal command + real serial\n";
    std::cout << "  real_robot              industrial camera + serial command + real serial\n";
    std::cout << "\n";
    std::cout << "Terminal commands in command-driven modes:\n";
    std::cout << "  arrive_bean <image_path> [print]\n";
    std::cout << "  arrive_digit <image_path> [print]\n";
    std::cout << "  camera mode: arrive_bean / arrive_digit\n";
    std::cout << "  reset\n";
    std::cout << "  quit\n";
}

/**
 * @brief 打印终端命令模式的可用命令。
 */
void printTerminalCommands() {
    std::cout << "Commands:\n";
    std::cout << "  arrive_bean <image_path> [print]\n";
    std::cout << "  arrive_digit <image_path> [print]\n";
    std::cout << "  camera mode: arrive_bean / arrive_digit\n";
    std::cout << "  reset\n";
    std::cout << "  quit\n";
}

/**
 * @brief 根据配置创建命令来源。
 * @param config 应用配置。
 * @param serial 串口模块，串口命令源会复用它。
 * @return 命令来源对象；source=none 时返回空指针。
 */
std::unique_ptr<CommandSource> makeCommandSource(const AppConfig& config, SerialPort& serial) {
    if (config.command.source == "terminal") {
        return std::make_unique<TerminalCommandSource>();
    }
    if (config.command.source == "serial") {
        return std::make_unique<SerialCommandSource>(serial);
    }
    if (config.command.source == "none") {
        return nullptr;
    }

    std::cerr << "Unknown command source: " << config.command.source << "\n";
    return nullptr;
}

/**
 * @brief 根据当前输入类型创建识别执行器。
 *
 * 本阶段只把 RecognitionRunner 纳入 main 的对象生命周期，
 * 不替换现有状态机调用链。
 */
std::unique_ptr<RecognitionRunner> makeRecognitionRunner(const AppConfig& config,
                                                         InputManager& input,
                                                         BeanNumberDetector& detector,
                                                         RoiParser& parser) {
    const bool multi_frame_input =
        config.input.type == "camera" ||
        config.input.type == "mindvision_camera" ||
        config.input.type == "video";

    if (multi_frame_input) {
        return std::make_unique<MultiFrameRecognitionRunner>(config, input, detector, parser);
    }

    return std::make_unique<SingleFrameRecognitionRunner>(input, input, detector, parser);
}

/**
 * @brief 运行命令驱动流程。
 */
void runCommandLoop(CommandSource& commandSource,
                    InputManager& input,
                    TaskStateMachine& taskStateMachine,
                    BeanNumberDetector& detector,
                    RoiParser& parser,
                    TaskGenerator& taskGenerator,
                    Protocol& protocol,
                    SerialPort& serial,
                    const AppConfig& config) {
    if (config.command.source == "terminal") {
        printTerminalCommands();
    }

    const bool camera_command_mode =
        config.input.type == "camera" ||
        config.input.type == "mindvision_camera" ||
        config.input.type == "video";
    MultiFrameRecognizer recognizer(config);
    if (camera_command_mode && !input.open()) {
        std::cerr << "Input open failed.\n";
        return;
    }

    std::string line;
    while (commandSource.next(line)) {
        const bool keep_running = camera_command_mode
            ? taskStateMachine.processCameraCommand(line, input, recognizer, detector, parser, taskGenerator, protocol, serial)
            : taskStateMachine.processCommand(line, detector, parser, taskGenerator, protocol, serial, config);
        if (!keep_running) {
            break;
        }
    }

    if (camera_command_mode) {
        input.release();
    }
}

/**
 * @brief 运行连续图像输入流程。
 */
bool runFrameLoop(InputManager& input,
                  TaskStateMachine& taskStateMachine,
                  BeanNumberDetector& detector,
                  RoiParser& parser,
                  TaskGenerator& taskGenerator,
                  Protocol& protocol,
                  SerialPort& serial,
                  const AppConfig& config) {
    if (!input.open()) {
        std::cerr << "Input open failed.\n";
        return false;
    }

    cv::Mat frame;
    while (input.read(frame)) {
        std::vector<Detection> detections = detector.detect(frame);
        VisionResult visionResult = parser.parse(detections);
        DebugLogger::saveCommandImages("frame_loop", config.input.source, frame, detections, visionResult, config, false);
        taskStateMachine.process(visionResult, taskGenerator, protocol, serial);

        if (config.debug.print_detections) {
            LogUtils::printDetections(detections);
        }
        if (config.debug.print_roi_result) {
            LogUtils::printVisionResult(visionResult);
        }
        if (config.debug.print_detections || config.debug.print_roi_result) {
            LogUtils::printTaskResult(taskStateMachine.lastTaskResult());
        }

        if (config.debug.draw_result || config.debug.show_window) {
            DrawUtils::drawDetections(frame, detections);
            DrawUtils::drawVisionResult(frame, visionResult);
        }
        if (config.debug.show_window) {
            cv::imshow("bean_vision_framework", frame);
            if (cv::waitKey(1) == 27) {
                break;
            }
        }

        if ((config.input.type == "mock" || config.input.type == "image") && taskStateMachine.done()) {
            break;
        }
    }

    input.release();
    return true;
}

}  // namespace

/**
 * @brief 程序入口，串起完整视觉到串口的数据流。
 * @param argc 命令行参数数量。
 * @param argv 命令行参数数组，argv[1] 可传入配置路径。
 * @return 程序正常结束返回 0，初始化或运行失败返回 1。
 */
int main(int argc, char** argv) {
    if (argc > 1) {
        const std::string first_arg = argv[1];
        if (first_arg == "--help" || first_arg == "-h") {
            printMainUsage(argv[0]);
            return 0;
        }
    }

    const std::string config_path = argc > 1 ? argv[1] : "../config/app.yaml";

    try {
        AppConfig config = AppConfig::load(config_path);
        std::cout << "Runtime mode: " << config.runtime.mode << "\n";

        InputManager input(config.input, config.camera);
        BeanNumberDetector detector(config.detector);
        RoiParser parser(config.roi);
        std::unique_ptr<RecognitionRunner> recognitionRunner =
            makeRecognitionRunner(config, input, detector, parser);
        TaskGenerator taskGenerator;
        TaskStateMachine taskStateMachine;
        Protocol protocol;
        SerialPort serial(config.serial);

        // RecognitionRunner 已纳入 main 生命周期，后续阶段再接入状态机调用链。
        (void)recognitionRunner;

        if (!detector.loadModel()) {
            std::cerr << "Detector load failed.\n";
            return 1;
        }
        if (!serial.open()) {
            std::cerr << "Serial open failed.\n";
            return 1;
        }

        std::unique_ptr<CommandSource> commandSource = makeCommandSource(config, serial);
        if (commandSource) {
            runCommandLoop(*commandSource, input, taskStateMachine, detector, parser, taskGenerator, protocol, serial, config);
        } else if (!runFrameLoop(input, taskStateMachine, detector, parser, taskGenerator, protocol, serial, config)) {
            serial.close();
            return 1;
        }

        serial.close();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
}
