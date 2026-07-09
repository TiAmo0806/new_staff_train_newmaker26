#include "task/TaskStateMachine.h"

#include "utils/DebugLogger.h"

#include <opencv2/imgcodecs.hpp>

#include <iostream>
#include <sstream>

namespace {

/**
 * @brief 将状态枚举转换为日志字符串。
 * @param state 当前状态。
 * @return 状态名称字符串。
 */
const char* stateName(TaskState state) {
    switch (state) {
    case TaskState::WAIT_BEAN_COMMAND:
        return "WAIT_BEAN_COMMAND";
    case TaskState::SCAN_BEANS:
        return "SCAN_BEANS";
    case TaskState::SEND_BEAN_BIND:
        return "SEND_BEAN_BIND";
    case TaskState::WAIT_DIGIT_COMMAND:
        return "WAIT_DIGIT_COMMAND";
    case TaskState::SCAN_DIGITS:
        return "SCAN_DIGITS";
    case TaskState::GENERATE_FINAL_TASK:
        return "GENERATE_FINAL_TASK";
    case TaskState::SEND_FINAL_TASK:
        return "SEND_FINAL_TASK";
    case TaskState::DONE:
        return "DONE";
    }
    return "UNKNOWN";
}

}  // namespace

/**
 * @brief 构造状态机，初始状态为 WAIT_BEAN_COMMAND。
 */
TaskStateMachine::TaskStateMachine() {
    logInitialState();
}

/**
 * @brief 处理一帧视觉结果，并在信息完整时推进状态。
 * @param current 当前帧 ROI 解析后的视觉结果。
 * @param taskGenerator 任务生成器。
 * @param protocol 协议打包器。
 * @param serial 串口发送模块。
 */
void TaskStateMachine::process(const VisionResult& current,
                               TaskGenerator& taskGenerator,
                               Protocol& protocol,
                               SerialPort& serial) {
    // mock 单帧可能同时包含豆子和数字，因此允许一次 process 连续推进。
    bool advance = true;

    while (advance) {
        advance = false;

        switch (state_) {
        case TaskState::SCAN_BEANS:
            memory_.updateBeans(current);
            if (memory_.beansReady()) {
                std::cout << "[MEMORY] beans ready\n";
                setState(TaskState::SCAN_DIGITS);
                advance = true;
            }
            break;

        case TaskState::SCAN_DIGITS:
            memory_.updateDigits(current);
            if (memory_.digitsReady()) {
                std::cout << "[MEMORY] digits ready\n";
                setState(TaskState::GENERATE_FINAL_TASK);
                advance = true;
            }
            break;

        case TaskState::GENERATE_FINAL_TASK:
            taskResult_ = taskGenerator.generate(memory_.mergedResult());
            if (taskResult_.success) {
                setState(TaskState::SEND_FINAL_TASK);
                advance = true;
            }
            break;

        case TaskState::SEND_FINAL_TASK:
            packet_ = protocol.makeTaskPacket(taskResult_);
            serial.write(packet_);
            memory_.clear();
            setState(TaskState::DONE);
            break;

        case TaskState::WAIT_BEAN_COMMAND:
        case TaskState::SEND_BEAN_BIND:
        case TaskState::WAIT_DIGIT_COMMAND:
        case TaskState::DONE:
            break;
        }
    }
}

bool TaskStateMachine::processCommand(const std::string& line,
                                      BeanNumberDetector& detector,
                                      RoiParser& parser,
                                      TaskGenerator& taskGenerator,
                                      Protocol& protocol,
                                      SerialPort& serial,
                                      const AppConfig& config) {
    // 命令格式保持简单：命令名 + 图片路径，便于用终端模拟机器人到位事件。
    std::istringstream iss(line);
    std::string command;
    std::string image_path;
    std::string debug_flag;
    iss >> command >> image_path >> debug_flag;
    const bool force_print = debug_flag == "print" || debug_flag == "--print" || debug_flag == "debug";

    if (command.empty()) {
        return true;
    }
    if (command == "quit") {
        return false;
    }
    if (command == "reset") {
        reset();
        return true;
    }
    if (command == "arrive_bean") {
        if (state_ != TaskState::WAIT_BEAN_COMMAND) {
            std::cout << "[WARN] expected arrive_digit <image_path>\n";
            return true;
        }
        return handleArriveBean(image_path, detector, parser, protocol, serial, config, force_print);
    }
    if (command == "arrive_digit") {
        if (state_ != TaskState::WAIT_DIGIT_COMMAND) {
            std::cout << "[WARN] expected arrive_bean <image_path>\n";
            return true;
        }
        return handleArriveDigit(image_path, detector, parser, taskGenerator, protocol, serial, config, force_print);
    }

    std::cout << "[WARN] unknown command: " << command << "\n";
    return true;
}

bool TaskStateMachine::processCameraCommand(const std::string& line,
                                            InputManager& input,
                                            MultiFrameRecognizer& recognizer,
                                            BeanNumberDetector& detector,
                                            RoiParser& parser,
                                            TaskGenerator& taskGenerator,
                                            Protocol& protocol,
                                            SerialPort& serial) {
    (void)detector;
    (void)parser;

    std::istringstream iss(line);
    std::string command;
    iss >> command;

    if (command.empty()) {
        return true;
    }
    if (command == "quit") {
        return false;
    }
    if (command == "reset") {
        reset();
        return true;
    }
    if (command == "arrive_bean") {
        if (state_ != TaskState::WAIT_BEAN_COMMAND) {
            std::cout << "[WARN] expected arrive_digit\n";
            return true;
        }
        std::cout << "[RX MOCK] ARRIVE_BEAN\n";
        setState(TaskState::SCAN_BEANS);
        VisionResult result = recognizer.scanBeans(input, detector, parser);
        if (!result.success) {
            setState(TaskState::WAIT_BEAN_COMMAND);
            return true;
        }
        return acceptBeanResult(result, protocol, serial);
    }
    if (command == "arrive_digit") {
        if (state_ != TaskState::WAIT_DIGIT_COMMAND) {
            std::cout << "[WARN] expected arrive_bean\n";
            return true;
        }
        std::cout << "[RX MOCK] ARRIVE_DIGIT\n";
        setState(TaskState::SCAN_DIGITS);
        VisionResult result = recognizer.scanDigits(input, detector, parser);
        if (!result.success) {
            setState(TaskState::WAIT_DIGIT_COMMAND);
            return true;
        }
        return acceptDigitResult(result, taskGenerator, protocol, serial);
    }

    std::cout << "[WARN] unknown command: " << command << "\n";
    return true;
}

/**
 * @brief 处理豆子区到位事件。
 * @param image_path 豆子区图片路径。
 * @param detector 检测器。
 * @param parser ROI 解析器。
 * @param protocol 协议打包器。
 * @param serial 串口发送模块。
 * @return 返回 true 表示继续等待后续命令。
 */
bool TaskStateMachine::handleArriveBean(const std::string& image_path,
                                        BeanNumberDetector& detector,
                                        RoiParser& parser,
                                        Protocol& protocol,
                                        SerialPort& serial,
                                        const AppConfig& config,
                                        bool force_print) {
    if (image_path.empty()) {
        std::cout << "[WARN] expected arrive_bean <image_path>\n";
        return true;
    }

    std::cout << "[RX MOCK] ARRIVE_BEAN image=" << image_path << "\n";
    cv::Mat frame = cv::imread(image_path);
    if (frame.empty()) {
        std::cout << "[ERROR] image not found: " << image_path << "\n";
        return true;
    }

    setState(TaskState::SCAN_BEANS);
    std::vector<Detection> detections = detector.detect(frame);
    std::cout << "[YOLO] detections=" << detections.size() << "\n";

    VisionResult result = parser.parse(detections);
    DebugLogger::saveCommandImages("arrive_bean", image_path, frame, detections, result, config, force_print);
    if (detections.empty()) {
        std::cout << "[WARN] no YOLO detections\n";
        setState(TaskState::WAIT_BEAN_COMMAND);
        return true;
    }

    return acceptBeanResult(result, protocol, serial);
}

/**
 * @brief 处理数字区到位事件。
 * @param image_path 数字区图片路径。
 * @param detector 检测器。
 * @param parser ROI 解析器。
 * @param taskGenerator 任务生成器。
 * @param protocol 协议打包器。
 * @param serial 串口发送模块。
 * @return 返回 true 表示继续等待后续命令。
 */
bool TaskStateMachine::handleArriveDigit(const std::string& image_path,
                                         BeanNumberDetector& detector,
                                         RoiParser& parser,
                                         TaskGenerator& taskGenerator,
                                         Protocol& protocol,
                                         SerialPort& serial,
                                         const AppConfig& config,
                                         bool force_print) {
    if (image_path.empty()) {
        std::cout << "[WARN] expected arrive_digit <image_path>\n";
        return true;
    }

    std::cout << "[RX MOCK] ARRIVE_DIGIT image=" << image_path << "\n";
    cv::Mat frame = cv::imread(image_path);
    if (frame.empty()) {
        std::cout << "[ERROR] image not found: " << image_path << "\n";
        return true;
    }

    setState(TaskState::SCAN_DIGITS);
    std::vector<Detection> detections = detector.detect(frame);
    std::cout << "[YOLO] detections=" << detections.size() << "\n";

    VisionResult result = parser.parse(detections);
    DebugLogger::saveCommandImages("arrive_digit", image_path, frame, detections, result, config, force_print);
    if (detections.empty()) {
        std::cout << "[WARN] no YOLO detections\n";
        setState(TaskState::WAIT_DIGIT_COMMAND);
        return true;
    }

    return acceptDigitResult(result, taskGenerator, protocol, serial);
}

bool TaskStateMachine::acceptBeanResult(const VisionResult& result,
                                        Protocol& protocol,
                                        SerialPort& serial) {
    memory_.updateBeans(result);
    if (!result.success || !memory_.beansReady()) {
        std::cout << "[WARN] no valid bean result in P1/P2/P3\n";
        setState(TaskState::WAIT_BEAN_COMMAND);
        return true;
    }

    for (const auto& bind : memory_.beanBinds()) {
        std::cout << "[BIND] " << bind.pickup_id << " "
                  << bind.bean_class << " -> " << bind.target_digit << "\n";
    }

    setState(TaskState::SEND_BEAN_BIND);
    packet_ = protocol.makeBeanBindPacket(memory_.beanBinds());
    serial.write(packet_);
    for (const auto& bind : memory_.beanBinds()) {
        std::cout << "[TX MOCK] BEAN_BIND pickup=" << bind.pickup_id
                  << " bean=" << bind.bean_class
                  << " target=" << bind.target_digit << "\n";
    }

    setState(TaskState::WAIT_DIGIT_COMMAND);
    return true;
}

bool TaskStateMachine::acceptDigitResult(const VisionResult& result,
                                         TaskGenerator& taskGenerator,
                                         Protocol& protocol,
                                         SerialPort& serial) {
    memory_.updateDigits(result);
    if (!memory_.digitsReady()) {
        std::cout << "[WARN] no valid digit result in L4-L8\n";
        setState(TaskState::WAIT_DIGIT_COMMAND);
        return true;
    }

    setState(TaskState::GENERATE_FINAL_TASK);
    taskResult_ = taskGenerator.generate(memory_.mergedResult());
    if (!taskResult_.success) {
        std::cout << "[WARN] final task generation failed: " << taskResult_.reason << "\n";
        setState(TaskState::WAIT_DIGIT_COMMAND);
        return true;
    }

    setState(TaskState::SEND_FINAL_TASK);
    packet_ = protocol.makeTaskPacket(taskResult_);
    serial.write(packet_);
    for (const auto& task : taskResult_.tasks) {
        std::cout << "[TX MOCK] FINAL_TASK from=P" << static_cast<int>(task.from)
                  << " to=L" << static_cast<int>(task.to)
                  << " bean=" << static_cast<int>(task.bean) << "\n";
    }

    setState(TaskState::DONE);
    return true;
}

/**
 * @brief 获取当前状态。
 * @return 当前 TaskState。
 */
TaskState TaskStateMachine::state() const {
    return state_;
}

/**
 * @brief 判断当前任务是否结束。
 * @return 状态为 DONE 时返回 true。
 */
bool TaskStateMachine::done() const {
    return state_ == TaskState::DONE;
}

/**
 * @brief 获取最近一次任务生成结果。
 * @return 最近一次 TaskResult。
 */
const TaskResult& TaskStateMachine::lastTaskResult() const {
    return taskResult_;
}

/**
 * @brief 获取只读视觉缓存。
 * @return 当前 VisionMemory。
 */
const VisionMemory& TaskStateMachine::memory() const {
    return memory_;
}

/**
 * @brief 清空缓存和任务结果，回到 WAIT_BEAN_COMMAND。
 */
void TaskStateMachine::reset() {
    memory_.clear();
    taskResult_ = TaskResult{};
    packet_.clear();
    state_ = TaskState::WAIT_BEAN_COMMAND;
    logInitialState();
}

/**
 * @brief 切换状态并输出状态日志。
 * @param next 下一个状态。
 */
void TaskStateMachine::setState(TaskState next) {
    if (state_ == next) {
        return;
    }
    state_ = next;
    std::cout << "[STATE] " << stateName(state_) << "\n";
}

/**
 * @brief 输出初始状态日志。
 */
void TaskStateMachine::logInitialState() const {
    std::cout << "[STATE] " << stateName(state_) << "\n";
}
