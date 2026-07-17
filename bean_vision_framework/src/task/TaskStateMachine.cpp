#include "task/TaskStateMachine.h"

#include "task/DigitInference.h"
#include "utils/DebugLogger.h"

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <iostream>
#include <map>
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

void printWaitHint(TaskState state) {
    switch (state) {
    case TaskState::WAIT_BEAN_COMMAND:
        std::cout << "[WAIT] type 'arrive_bean' to scan beans, or 'reset'/'quit'\n";
        break;
    case TaskState::WAIT_DIGIT_COMMAND:
        std::cout << "[WAIT] type 'arrive_digit' to scan digits, or 'reset'/'quit'\n";
        break;
    case TaskState::DONE:
        std::cout << "[WAIT] task done, type 'reset' to start a new round or 'quit'\n";
        break;
    default:
        break;
    }
}

const PositionResult& digitPositionByPlace(const VisionResult& result, int place_id) {
    switch (place_id) {
    case 4:
        return result.l4;
    case 5:
        return result.l5;
    case 6:
        return result.l6;
    case 7:
        return result.l7;
    case 8:
        return result.l8;
    default:
        return result.l4;
    }
}

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

void printDigitSnapshot(const VisionResult& result) {
    const std::map<int, std::string> place_labels = {
        {4, "L4"},
        {5, "L5"},
        {6, "L6"},
        {7, "L7"},
        {8, "L8"},
    };

    for (const auto& [place_id, label] : place_labels) {
        const PositionResult& position = digitPositionByPlace(result, place_id);
        const int digit = digitValue(position);
        if (digit > 0) {
            std::cout << "[DIGIT] " << label << " = digit_" << digit << "\n";
        } else {
            std::cout << "[DIGIT] " << label << " = unknown\n";
        }
    }
}

void applyInferredDigits(VisionResult& result, const DigitInferenceResult& inference_result) {
    auto apply = [&](PositionResult& position, int place_id) {
        const auto it = inference_result.place_to_digit.find(place_id);
        if (it == inference_result.place_to_digit.end()) {
            return;
        }
        position.valid = true;
        position.position_id = "L" + std::to_string(place_id);
        position.class_id = it->second + 2;
        position.class_name = "digit_" + std::to_string(it->second);
    };

    for (int place_id : inference_result.inferred_places) {
        switch (place_id) {
        case 4:
            apply(result.l4, place_id);
            break;
        case 5:
            apply(result.l5, place_id);
            break;
        case 6:
            apply(result.l6, place_id);
            break;
        case 7:
            apply(result.l7, place_id);
            break;
        case 8:
            apply(result.l8, place_id);
            break;
        default:
            break;
        }
    }
}

void printDigitSnapshot(const VisionResult& result, const DigitInferenceResult& inference_result) {
    const std::map<int, std::string> place_labels = {
        {4, "L4"},
        {5, "L5"},
        {6, "L6"},
        {7, "L7"},
        {8, "L8"},
    };

    for (const auto& [place_id, label] : place_labels) {
        const auto digit_it = inference_result.place_to_digit.find(place_id);
        if (digit_it != inference_result.place_to_digit.end()) {
            const bool inferred =
                std::find(inference_result.inferred_places.begin(),
                          inference_result.inferred_places.end(),
                          place_id) != inference_result.inferred_places.end();
            std::cout << "[DIGIT] " << label << " = digit_" << digit_it->second;
            if (inferred) {
                std::cout << " inferred";
            }
            std::cout << "\n";
            continue;
        }

        const PositionResult& position = digitPositionByPlace(result, place_id);
        const int digit = digitValue(position);
        if (digit > 0) {
            std::cout << "[DIGIT] " << label << " = digit_" << digit << "\n";
        } else {
            std::cout << "[DIGIT] " << label << " = unknown\n";
        }
    }
}

std::string formatMissingPlaces(const std::vector<int>& missing_places) {
    std::ostringstream oss;
    for (size_t i = 0; i < missing_places.size(); ++i) {
        if (i > 0) {
            oss << ", ";
        }
        oss << "L" << missing_places[i];
    }
    return oss.str();
}

VisionResult mergeBeanAndDigitResult(const VisionResult& bean_result, const VisionResult& digit_result) {
    VisionResult merged = bean_result;
    merged.l4 = digit_result.l4;
    merged.l5 = digit_result.l5;
    merged.l6 = digit_result.l6;
    merged.l7 = digit_result.l7;
    merged.l8 = digit_result.l8;
    merged.success = true;
    merged.reason = "ok";
    return merged;
}

void printTaskPreview(const TaskResult& result) {
    std::cout << "[TASK PREVIEW] " << (result.success ? "success" : "fail")
              << " reason=" << result.reason << "\n";
    for (const auto& task : result.tasks) {
        std::cout << "[TASK PREVIEW] P" << static_cast<int>(task.from)
                  << " -> L" << static_cast<int>(task.to)
                  << " bean=" << static_cast<int>(task.bean) << "\n";
    }
}

bool finalTaskReady(const TaskResult& result) {
    return result.success && result.tasks.size() == 3;
}

void logIncompleteFinalTasks(const TaskResult& result) {
    std::cout << "[ERROR] incomplete final tasks count=" << result.tasks.size()
              << " expected=3, skip FINAL_TASK send\n";
}

}  // namespace

/**
 * @brief 构造状态机，初始状态为 WAIT_BEAN_COMMAND。
 */
TaskStateMachine::TaskStateMachine(RecognitionRunner& runner) : runner_(runner) {
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
            if (!finalTaskReady(taskResult_)) {
                logIncompleteFinalTasks(taskResult_);
                setState(TaskState::WAIT_DIGIT_COMMAND);
                break;
            }
            packet_ = protocol.makeTaskPacket(taskResult_);
            if (!serial.write(packet_)) {
                std::cout << "[ERROR] failed to send FINAL_TASK\n";
                setState(TaskState::WAIT_DIGIT_COMMAND);
                break;
            }
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
                                      TaskGenerator& taskGenerator,
                                      Protocol& protocol,
                                      SerialPort& serial) {
    // 命令格式保持简单：命令名，不接受额外业务参数。
    std::istringstream iss(line);
    std::string command;
    std::string extra;
    iss >> command >> extra;

    if (command.empty()) {
        return true;
    }
    if (command == "quit") {
        if (!extra.empty()) {
            std::cout << "[WARN] command does not take arguments: " << command << "\n";
            return true;
        }
        return false;
    }
    if (command == "reset") {
        if (!extra.empty()) {
            std::cout << "[WARN] command does not take arguments: " << command << "\n";
            return true;
        }
        reset();
        return true;
    }
    if (command == "arrive_bean") {
        if (!extra.empty()) {
            std::cout << "[WARN] command does not take arguments: " << command << "\n";
            return true;
        }
        if (state_ != TaskState::WAIT_BEAN_COMMAND) {
            std::cout << "[WARN] expected arrive_digit\n";
            return true;
        }
        return handleArriveBean(protocol, serial);
    }
    if (command == "arrive_digit") {
        if (!extra.empty()) {
            std::cout << "[WARN] command does not take arguments: " << command << "\n";
            return true;
        }
        if (state_ != TaskState::WAIT_DIGIT_COMMAND) {
            std::cout << "[WARN] expected arrive_bean\n";
            return true;
        }
        return handleArriveDigit(taskGenerator, protocol, serial);
    }

    std::cout << "[WARN] unknown command: " << command << "\n";
    return true;
}

/**
 * @brief 处理豆子区到位事件。
 * @param protocol 协议打包器。
 * @param serial 串口发送模块。
 * @return 返回 true 表示继续等待后续命令。
 */
bool TaskStateMachine::handleArriveBean(Protocol& protocol,
                                        SerialPort& serial) {
    std::cout << "[RX COMMAND] ARRIVE_BEAN\n";
    setState(TaskState::SCAN_BEANS);

    VisionResult result;
    if (!runner_.scanBeans(result)) {
        std::cout << "[RECOGNITION][BEAN][FAILED] reason=" << result.reason
                  << " bean_bind_sent=false"
                  << " action=wait_for_arrive_bean\n";
        setState(TaskState::WAIT_BEAN_COMMAND);
        return true;
    }

    return acceptBeanResult(result, protocol, serial);
}

/**
 * @brief 处理数字区到位事件。
 * @param taskGenerator 任务生成器。
 * @param protocol 协议打包器。
 * @param serial 串口发送模块。
 * @return 返回 true 表示继续等待后续命令。
 */
bool TaskStateMachine::handleArriveDigit(TaskGenerator& taskGenerator,
                                         Protocol& protocol,
                                         SerialPort& serial) {
    std::cout << "[RX COMMAND] ARRIVE_DIGIT\n";
    setState(TaskState::SCAN_DIGITS);

    VisionResult result;
    if (!runner_.scanDigits(result)) {
        std::cout << "[RECOGNITION][DIGIT][FAILED] reason=" << result.reason
                  << " final_task_sent=false"
                  << " bean_binding=preserved"
                  << " action=wait_for_arrive_digit\n";
        setState(TaskState::WAIT_DIGIT_COMMAND);
        return true;
    }

    DigitInference inference;
    const DigitInferenceResult inference_result = inference.analyze(result);
    printDigitSnapshot(result, inference_result);
    if (!inference_result.complete) {
        std::cout << "[DIGIT] incomplete: missing "
                  << formatMissingPlaces(inference_result.missing_places) << "\n";
        setState(TaskState::WAIT_DIGIT_COMMAND);
        return true;
    }

    if (inference_result.inferred) {
        std::cout << "[DIGIT] complete by inference\n";
    } else {
        std::cout << "[DIGIT] complete\n";
    }

    VisionResult inferred_result = result;
    applyInferredDigits(inferred_result, inference_result);
    const VisionResult preview_vision = mergeBeanAndDigitResult(memory_.mergedResult(), inferred_result);
    const TaskResult preview_task = taskGenerator.generate(preview_vision);
    printTaskPreview(preview_task);

    if (!preview_task.success) {
        std::cout << "[WARN] final task generation failed: " << preview_task.reason << "\n";
        setState(TaskState::WAIT_DIGIT_COMMAND);
        return true;
    }
    if (!finalTaskReady(preview_task)) {
        logIncompleteFinalTasks(preview_task);
        setState(TaskState::WAIT_DIGIT_COMMAND);
        return true;
    }

    taskResult_ = preview_task;
    setState(TaskState::SEND_FINAL_TASK);
    packet_ = protocol.makeTaskPacket(taskResult_);
    if (!serial.write(packet_)) {
        std::cout << "[ERROR] failed to send FINAL_TASK\n";
        setState(TaskState::WAIT_DIGIT_COMMAND);
        return true;
    }

    for (const auto& task : taskResult_.tasks) {
        std::cout << "[TX FINAL_TASK] P" << static_cast<int>(task.from)
                  << " -> L" << static_cast<int>(task.to)
                  << " bean=" << static_cast<int>(task.bean) << "\n";
    }

    setState(TaskState::DONE);
    return true;
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
    if (memory_.beanBinds().size() != 3) {
        std::cout << "[ERROR] incomplete bean binds count=" << memory_.beanBinds().size()
                  << " expected=3, skip BEAN_BIND send\n";
        setState(TaskState::WAIT_BEAN_COMMAND);
        return true;
    }

    for (const auto& bind : memory_.beanBinds()) {
        std::cout << "[BIND] " << bind.pickup_id << " "
                  << bind.bean_class << " -> " << bind.target_digit << "\n";
    }

    setState(TaskState::SEND_BEAN_BIND);
    packet_ = protocol.makeBeanBindPacket(memory_.beanBinds());
    if (!serial.write(packet_)) {
        std::cout << "[ERROR] BEAN_BIND send failed or ACK timeout\n";
        setState(TaskState::WAIT_BEAN_COMMAND);
        return true;
    }
    for (const auto& bind : memory_.beanBinds()) {
        std::cout << "[TX BEAN_BIND] pickup=" << bind.pickup_id
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
    if (!finalTaskReady(taskResult_)) {
        logIncompleteFinalTasks(taskResult_);
        setState(TaskState::WAIT_DIGIT_COMMAND);
        return true;
    }

    setState(TaskState::SEND_FINAL_TASK);
    packet_ = protocol.makeTaskPacket(taskResult_);
    if (!serial.write(packet_)) {
        std::cout << "[ERROR] failed to send FINAL_TASK\n";
        setState(TaskState::WAIT_DIGIT_COMMAND);
        return true;
    }
    for (const auto& task : taskResult_.tasks) {
        std::cout << "[TX FINAL_TASK] P" << static_cast<int>(task.from)
                  << " -> L" << static_cast<int>(task.to)
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
    printWaitHint(state_);
}

/**
 * @brief 输出初始状态日志。
 */
void TaskStateMachine::logInitialState() const {
    std::cout << "[STATE] " << stateName(state_) << "\n";
    printWaitHint(state_);
}
