#pragma once

#include "communication/Protocol.h"
#include "communication/SerialPort.h"
#include "core/AppConfig.h"
#include "core/TaskTypes.h"
#include "core/VisionResult.h"
#include "detector/BeanNumberDetector.h"
#include "input/InputManager.h"
#include "parser/RoiParser.h"
#include "recognition/MultiFrameRecognizer.h"
#include "task/TaskGenerator.h"
#include "task/VisionMemory.h"

#include <string>
#include <vector>

/**
 * @brief 当前任务流程状态。
 */
enum class TaskState {
    WAIT_BEAN_COMMAND,
    SCAN_BEANS,     // 扫描豆子区 P1/P2/P3。
    SEND_BEAN_BIND,
    WAIT_DIGIT_COMMAND,
    SCAN_DIGITS,    // 扫描数字区 L4-L8。
    GENERATE_FINAL_TASK,
    SEND_FINAL_TASK,
    DONE            // 当前任务结束。
};

/**
 * @brief 控制视觉识别到任务发送的状态流转。
 *
 * TaskStateMachine 只负责任务阶段切换。
 * 具体检测、ROI 解析、任务生成、协议打包和串口发送仍交给对应模块。
 */
class TaskStateMachine {
public:
    /**
     * @brief 构造状态机，初始状态为 WAIT_BEAN_COMMAND。
     */
    TaskStateMachine();

    /**
     * @brief 处理一帧视觉结果，并在信息完整时推进状态。
     * @param current 当前帧 ROI 解析后的视觉结果。
     * @param taskGenerator 任务生成器。
     * @param protocol 协议打包器。
     * @param serial 串口发送模块。
     */
    void process(const VisionResult& current,
                 TaskGenerator& taskGenerator,
                 Protocol& protocol,
                 SerialPort& serial);

    /**
     * @brief 处理命令行模拟事件。
     * @param line 用户输入的一行命令，例如 arrive_bean <image_path>。
     * @param detector 检测器，用于对命令携带的图片执行 YOLO 推理。
     * @param parser ROI 解析器，用于把检测框映射到固定位置。
     * @param taskGenerator 任务生成器。
     * @param protocol 协议打包器。
     * @param serial 串口发送模块。
     * @param config 总配置，用于控制命令模式调试输出。
     * @return 返回 false 表示收到 quit，需要退出命令循环。
     */
    bool processCommand(const std::string& line,
                        BeanNumberDetector& detector,
                        RoiParser& parser,
                        TaskGenerator& taskGenerator,
                        Protocol& protocol,
                        SerialPort& serial,
                        const AppConfig& config);

    /**
     * @brief 处理相机调试模式下的终端命令。
     * @param line 用户输入的一行命令，例如 arrive_bean。
     * @param input 已打开并保持取流的输入源。
     * @param recognizer 多帧稳定识别器。
     * @param detector 检测器。
     * @param parser ROI 解析器。
     * @param taskGenerator 任务生成器。
     * @param protocol 协议打包器。
     * @param serial 串口发送模块。
     * @return 返回 false 表示收到 quit，需要退出命令循环。
     */
    bool processCameraCommand(const std::string& line,
                              InputManager& input,
                              MultiFrameRecognizer& recognizer,
                              BeanNumberDetector& detector,
                              RoiParser& parser,
                              TaskGenerator& taskGenerator,
                              Protocol& protocol,
                              SerialPort& serial);

    /**
     * @brief 获取当前状态。
     * @return 当前 TaskState。
     */
    TaskState state() const;

    /**
     * @brief 判断当前任务是否结束。
     * @return 状态为 DONE 时返回 true。
     */
    bool done() const;

    /**
     * @brief 获取最近一次任务生成结果。
     * @return 最近一次 TaskResult。
     */
    const TaskResult& lastTaskResult() const;

    /**
     * @brief 获取只读视觉缓存。
     * @return 当前 VisionMemory。
     */
    const VisionMemory& memory() const;

    /**
     * @brief 清空缓存和任务结果，回到 WAIT_BEAN_COMMAND。
     */
    void reset();

private:
    /**
     * @brief 处理“到达豆子区”命令。
     * @param image_path 豆子区图片路径。
     * @param detector 检测器。
     * @param parser ROI 解析器。
     * @param protocol 协议打包器。
     * @param serial 串口发送模块。
     * @param config 总配置。
     * @param force_print 是否强制保存调试图片。
     * @return 命令循环是否继续执行。
     */
    bool handleArriveBean(const std::string& image_path,
                          BeanNumberDetector& detector,
                          RoiParser& parser,
                          Protocol& protocol,
                          SerialPort& serial,
                          const AppConfig& config,
                          bool force_print);

    /**
     * @brief 处理“到达数字区”命令。
     * @param image_path 数字区图片路径。
     * @param detector 检测器。
     * @param parser ROI 解析器。
     * @param taskGenerator 任务生成器。
     * @param protocol 协议打包器。
     * @param serial 串口发送模块。
     * @param config 总配置。
     * @param force_print 是否强制保存调试图片。
     * @return 命令循环是否继续执行。
     */
    bool handleArriveDigit(const std::string& image_path,
                           BeanNumberDetector& detector,
                           RoiParser& parser,
                           TaskGenerator& taskGenerator,
                           Protocol& protocol,
                           SerialPort& serial,
                           const AppConfig& config,
                           bool force_print);

    bool acceptBeanResult(const VisionResult& result,
                          Protocol& protocol,
                          SerialPort& serial);

    bool acceptDigitResult(const VisionResult& result,
                           TaskGenerator& taskGenerator,
                           Protocol& protocol,
                           SerialPort& serial);

    /**
     * @brief 切换状态并输出状态日志。
     * @param next 下一个状态。
     */
    void setState(TaskState next);

    /**
     * @brief 输出初始状态日志。
     */
    void logInitialState() const;

    TaskState state_ = TaskState::WAIT_BEAN_COMMAND;
    VisionMemory memory_;
    TaskResult taskResult_;
    std::vector<uint8_t> packet_;
};
