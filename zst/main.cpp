/**
 * @file main.cpp
 * @brief 起重机物流视觉主程序
 *   相机相关问题 -> CameraDriver/
 *   YOLO/多帧投票/规则判断 -> ImgProcessing/
 *   串口协议 -> Communication/
 *   配置读取 -> Tool/
 */

#include "CameraDriver/MindVisionCamera.h"
#include "Communication/VisionProtocol.h"
#include "ImgProcessing/CompetitionWorkflow.h"
#include "Communication/VirtualSerial.h"
#include "ImgProcessing/VisionSystem.h"
#include "Tool/Utils.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <sstream>
#include <string>
#include <thread>

namespace
{
// 将当前帧有效检测按框中心X从左到右排列。
// 这里只用于debug日志和画面提示，不会改变工作流的多帧投票、正式排序和发送数据。
std::vector<Detection> leftToRightDetections(const std::vector<Detection> &detections)
{
    std::vector<Detection> sorted;
    for (const Detection &d : detections)
    {
        if (d.kind == TargetKind::Bean || d.kind == TargetKind::DigitBox)
            sorted.push_back(d);
    }
    std::sort(sorted.begin(), sorted.end(), [](const Detection &a, const Detection &b) {
        return a.box.x + a.box.width / 2 < b.box.x + b.box.width / 2;
    });
    return sorted;
}

// OpenCV内置字体不能可靠显示中文，因此窗口上使用短英文；终端仍打印完整中文说明。
std::string detectionOverlayText(const std::vector<Detection> &sorted)
{
    std::ostringstream oss;
    oss << "YOLO L-R:";
    for (const Detection &d : sorted)
    {
        if (d.kind == TargetKind::DigitBox)
            oss << " D" << d.digit;
        else
            oss << " B" << static_cast<int>(encodeBeanType(d.bean));
    }
    if (sorted.empty()) oss << " NONE";
    return oss.str();
}

std::string digitLayoutKey(const FieldState &state)
{
    std::ostringstream oss;
    for (int value : state.boxPlaces) oss << value << ',';
    return oss.str();
}
}

int main(int argc, char **argv)
{
    // 显式传参时优先使用用户给出的配置；未传参时根据可执行文件位置查找。
    // 因此无论从 build 目录、项目根目录还是 VS Code 启动，都不会依赖当前工作目录。
    const std::string configPath = argc > 1
        ? std::string(argv[1])                          // 命令行传入的配置文件路径
        : findDefaultConfigPath(argv[0]).string();      // 自动查找默认配置

    std::cout << "[Main] 配置文件: " << configPath << std::endl;

    // 1. 读取配置。
    // 配置文件里放模型路径、相机参数、串口参数、是否显示窗口等。
    // 这些参数不写死在代码里，方便比赛现场快速改。
    AppConfig config;
    if (!loadAppConfig(configPath, config))             // 从 YAML 加载全部配置
    {
        // 模型路径也来自配置文件，继续运行只会在OpenVINO加载ONNX时再次报错。
        std::cerr << "[Main] 配置文件加载失败，程序退出" << std::endl;
        return 1;                                       // 非零退出码表示异常
    }

    // 2. 先创建工业相机对象，实际打开时机由runtime.mode决定。
    // competition：启动时关闭，收到电控camera_state=1后才打开；
    // debug：启动后主循环立即打开，不需要等待电控，便于日常调模型和画面。
    MindVisionCamera camera(config.camera);             // 保存相机配置，暂不打开设备
    bool cameraIsOpen = false;                          // SDK当前没有持有相机句柄
    bool cameraEnabled = config.runMode == AppRunMode::Debug;

    // 3. 优先初始化串口。
    // 串口放在OpenVINO模型加载之前打开，尽量避免启动阶段电控已发送camera_state=1，
    // 但视觉端还没有打开串口而错过控制帧。
    VirtualSerial serial(config.serial);                // 创建双向串口
    if (!serial.openPort())
    {
        std::cerr << "[Main] 串口打开失败，接收循环将每秒尝试扫描设备并重连"
                  << std::endl;
    }

    // 4. 初始化视觉系统。
    // VisionSystem 内部包含：
    //   OpenVINO直接加载best5.onnx并执行YOLO检测；
    //   TaskPlanner 比赛规则决策。
    // 模型可以提前加载；比赛模式等待camera_state=1，调试模式会直接开始逐帧推理。
    VisionSystem vision(config.vision);                 // 创建视觉系统

    // 两队共用识别算法、协议编码和物理串口，只切换上层流程状态机。
    // TeamA：3个豆子 -> 5个数字 -> 一次发送6字节位置结果。
    // 数字识别缓存由A/B共用：每个角度凑齐配置数量后，按多帧平均X从左到右整批保存。
    // TeamB：中心第1个豆子 -> 全部数字place1~5 -> 中心剩余新豆子；类型顺序不固定。
    CompetitionWorkflow workflow(config.workflow);      // 创建比赛工作流
    std::cout << "[Main] 当前队伍: " << teamModeToString(config.workflow.mode)
              << "，投票帧数=" << config.workflow.voteFramesPerStage
              << "，最少命中=" << config.workflow.minHitsPerStage
              << "，每角度数字数=" << config.workflow.digitsPerView
              << "，place5推断="
              << (config.workflow.inferPlace5FromFirstFour ? "开启" : "关闭")
              << "，B组中心区域宽度="
              << (config.workflow.teamBCenterWidthRatio * 100.0f) << "%" << std::endl;
    std::cout << "[Main] 串口模式: " << (config.serial.simulated ? "模拟发送" : "真实串口")
              << "，端口=" << config.serial.port
              << "，波特率=" << config.serial.baudrate
              << "，TX日志=" << (config.serial.txLog ? "开启" : "关闭")
              << "，RX日志=" << (config.serial.rxLog ? "开启" : "关闭") << std::endl;
    if (config.runMode == AppRunMode::Debug)
    {
        std::cout << "[CameraControl] 当前为debug调试模式：程序将直接打开相机，"
                     "不等待并忽略电控camera_state开关命令"
                  << std::endl;
        if (!config.serial.simulated)
            std::cout << "[CameraControl] 调试模式仍保留真实双向串口通信；"
                         "若没有连接电控，建议把serial.simulated改为true"
                      << std::endl;
    }
    else
    {
        std::cout << "[CameraControl] 当前为competition比赛模式：相机保持关闭，"
                     "等待电控发送camera_state=1"
                  << std::endl;
        if (config.serial.simulated)
        {
            // 模拟串口没有真实RX输入，比赛模式下不会自动收到开启命令。
            std::cerr << "[CameraControl] 警告：competition和simulated=true同时使用时，"
                         "相机会一直关闭；实机比赛请把simulated改为false"
                      << std::endl;
        }
    }

    const std::string windowName = "Logistics Vision";
    if (config.showWindow) cv::namedWindow(windowName, cv::WINDOW_NORMAL); // 创建可缩放窗口

    // 三个位置都会处理按键：正常取图、相机取图失败、以及电控主动关闭相机时。
    // 返回true表示用户要求退出；R/1/2仍交给工作流处理，不依赖相机是否开启。
    auto handleKey = [&](int key) {
        if (key == 27 || key == 'q' || key == 'Q') return true; // ESC或Q退出程序
        if (key == '1') workflow.switchMode(TeamMode::TeamA);   // 切换到队伍A
        if (key == '2') workflow.switchMode(TeamMode::TeamB);   // 切换到队伍B
        // 新一场比赛开始前按R：同时清空内存投票和磁盘断点。
        // 正常中途关相机不要按R，否则会主动删除断点并回到第一阶段。
        if (key == 'r' || key == 'R') workflow.reset();
        return false;
    };

    int consecutiveCaptureFailures = 0;
    cv::Mat lastDebugImage;                            // 相机短暂掉线时保持窗口可响应
    std::string lastDebugFinalLayout;                  // 最终5位数组只打印一次

    auto sendWorkflowResult = [&](const VisionTxPacket &packet) {
        if (config.runMode == AppRunMode::Debug)
        {
            // 这里打印的DATA就是电控在CMD之后实际解析的业务字节，不包含日志文字。
            std::cout << "[Debug待发送] CMD=0x" << std::hex << std::uppercase
                      << std::setw(2) << std::setfill('0')
                      << static_cast<int>(packet.command) << std::dec
                      << std::setfill(' ') << "，DATA(十进制)=[";
            for (std::size_t i = 0; i < packet.data.size(); ++i)
            {
                if (i != 0) std::cout << ' ';
                std::cout << static_cast<int>(packet.data[i]);
            }
            std::cout << "]" << std::endl;
        }
        std::cout << "[Serial] 发送识别结果：cmd=0x" << std::hex << std::uppercase
                  << static_cast<int>(packet.command) << std::dec << std::endl;
        const bool written = serial.sendPacket(packet);
        if (!written)
            std::cerr << "[Serial] 本次识别结果发送失败；无ACK模式不会自动重发"
                      << std::endl;
        return written;
    };

    while (true)
    {
        // 4. 先处理电控->视觉的4字节相机控制帧，再决定本轮是否取图。
        // receiveCameraState()非阻塞，可处理半帧、粘包和线路噪声。
        uint8_t requestedCameraState = 0;
        while (serial.receiveCameraState(requestedCameraState))
        {
            // 调试模式的相机始终由本机配置控制，不允许偶然收到的0将相机关闭。
            if (config.runMode == AppRunMode::Debug)
            {
                std::cout << "[CameraControl] debug模式忽略电控camera_state="
                          << static_cast<int>(requestedCameraState) << std::endl;
                continue;
            }

            const bool requestedEnabled = requestedCameraState == 1;
            if (requestedEnabled == cameraEnabled) continue; // 重复命令无需反复开关SDK

            cameraEnabled = requestedEnabled;
            if (!cameraEnabled)
            {
                // 只关闭相机SDK，不退出程序、不关闭串口，也不清空A/B识别和断点数据。
                // 因此A组识别完豆子后收到0，之后再收到1仍会直接进入数字阶段。
                if (cameraIsOpen) camera.close();
                cameraIsOpen = false;
                consecutiveCaptureFailures = 0;
                std::cout << "[CameraControl] 收到camera_state=0，相机已关闭，串口继续监听"
                          << std::endl;
            }
            else
            {
                // 不在接收函数里直接open，避免串口协议层依赖相机SDK；
                // 本轮下面的统一重连分支会执行camera.open()。
                std::cout << "[CameraControl] 收到camera_state=1，准备打开相机" << std::endl;
            }
        }

        // 电控要求关闭相机时，不能调用camera.read()，否则SDK会持续报取帧失败并触发看门狗。
        // 这里仍刷新窗口、处理键盘并短暂休眠，保证程序可退出且不会空循环占满CPU。
        if (!cameraEnabled)
        {
            if (config.showWindow)
            {
                if (!lastDebugImage.empty()) cv::imshow(windowName, lastDebugImage);
                if (handleKey(cv::waitKey(1))) break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // camera_state=1但相机尚未打开，或之前USB掉线导致句柄关闭时，在这里统一重试。
        if (!cameraIsOpen)
        {
            std::cout << "[CameraControl] 正在打开相机" << std::endl;
            cameraIsOpen = camera.open();
            if (cameraIsOpen)
            {
                consecutiveCaptureFailures = 0;
                std::cout << "[CameraControl] 相机打开成功" << std::endl;
            }
            else
            {
                std::cerr << "[CameraControl] 相机打开失败，1秒后继续尝试" << std::endl;
                if (config.showWindow && handleKey(cv::waitKey(1))) break;
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
        }

        // 5. 取图。
        // frame 是一帧 BGR 图像，后面会直接送进 YOLO。
        // 如果连续取图失败，自动重开相机；失败期间仍调用waitKey，避免窗口假死。
        cv::Mat frame;
        if (!camera.read(frame) || frame.empty())
        {
            consecutiveCaptureFailures++;
            if (consecutiveCaptureFailures == 1 ||
                consecutiveCaptureFailures >= config.camera.reconnectAfterFailures)
            {
                std::cerr << "[CameraWatchdog] 取图失败，连续次数="
                          << consecutiveCaptureFailures << "/"
                          << config.camera.reconnectAfterFailures << std::endl;
            }

            // 原代码在这里直接continue，导致OpenCV窗口不处理事件，只能强制停止。
            if (config.showWindow)
            {
                if (!lastDebugImage.empty()) cv::imshow(windowName, lastDebugImage);
                if (handleKey(cv::waitKey(1))) break;
            }

            if (consecutiveCaptureFailures >= config.camera.reconnectAfterFailures)
            {
                std::cerr << "[CameraWatchdog] 尝试重新打开相机" << std::endl;
                camera.close();
                cameraIsOpen = false;
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                cameraIsOpen = camera.open();
                if (cameraIsOpen)
                {
                    std::cout << "[CameraWatchdog] 相机重连成功" << std::endl;
                    consecutiveCaptureFailures = 0;
                }
                else
                {
                    std::cerr << "[CameraWatchdog] 相机重连失败，1秒后继续尝试" << std::endl;
                    consecutiveCaptureFailures = config.camera.reconnectAfterFailures;
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            }
            continue;
        }
        consecutiveCaptureFailures = 0;

        // 6. OpenVINO YOLO检测 + 任务规划。
        // result.detections：当前帧所有豆子/数字箱检测框；
        // result.decision：根据比赛规则算出的目标数字箱；
        // result.debugImage：画好框和文字的调试图。
        VisionFrameResult result = vision.process(frame); // 完整一帧视觉处理

        // debug模式只在图像窗口显示当前帧YOLO结果，不再把单帧抖动持续打印到终端。
        // 终端只保留收集器输出的多帧稳定核对、排序、缓存和最终发送结果。
        std::vector<Detection> debugSortedDetections;
        if (config.runMode == AppRunMode::Debug)
        {
            debugSortedDetections = leftToRightDetections(result.detections);
            cv::putText(result.debugImage, detectionOverlayText(debugSortedDetections),
                        cv::Point(20, 75), cv::FONT_HERSHEY_SIMPLEX, 0.65,
                        cv::Scalar(255, 255, 0), 2);
        }

        // 7. 当前队伍工作流决定"这一帧是否产生阶段消息"。
        // update只处理识别顺序和生成消息，不直接操作串口，因此TeamA/B流程可以独立测试。
        // 无ACK模式下，结果生成后工作流立即进入下一阶段，并只发送一次。
        // imageWidth用于B组选择最靠近画面中心的豆子；A组仍按原来的从左到右规则。
        const std::vector<VisionTxPacket> packets =
            workflow.update(result.detections, frame.cols); // 工作流更新
        for (const VisionTxPacket &tx : packets)
        {
            // 当前每个阶段最多产生一条结果，直接通过串口发送，不等待电控确认。
            sendWorkflowResult(tx);
        }

        // place1~place4按多帧平均X排序确认后，place5由15减去前四位数字之和推断。
        // 这里同时在debug终端和画面显示最终五位数组；两队共用同一套数字缓存逻辑。
        if (config.runMode == AppRunMode::Debug && workflow.state().boxReady)
        {
            const FieldState &state = workflow.state();
            const std::string layoutKey = digitLayoutKey(state);
            if (layoutKey != lastDebugFinalLayout)
            {
                std::cout << "[Debug最终推断] 数字布局=[place1=" << state.boxPlaces[0]
                          << ", place2=" << state.boxPlaces[1]
                          << ", place3=" << state.boxPlaces[2]
                          << ", place4=" << state.boxPlaces[3]
                          << ", place5=" << state.boxPlaces[4] << "]" << std::endl;
                lastDebugFinalLayout = layoutKey;
            }
            std::ostringstream overlay;
            overlay << "FINAL P1-P5:";
            for (int digit : state.boxPlaces) overlay << ' ' << digit;
            cv::putText(result.debugImage, overlay.str(), cv::Point(20, 105),
                        cv::FONT_HERSHEY_SIMPLEX, 0.70, cv::Scalar(0, 255, 255), 2);
        }

        // 保存最近一张识别结果图。相机短暂掉线时继续显示该图并处理退出按键。
        // 此处不再叠加FPS、取帧耗时或推理延迟，避免比赛日志和画面被性能信息干扰。
        lastDebugImage = result.debugImage;

        // 8. 调试窗口显示。
        // 按 ESC / q 退出程序。
        // 实车跑无屏幕模式时，可以在配置里关闭 show_window。
        if (config.showWindow)
        {
            cv::imshow(windowName, result.debugImage);  // 显示调试图
            if (handleKey(cv::waitKey(1))) break;       // 获取并统一处理按键
        }
    }

    serial.closePort();                                 // 关闭串口
    camera.close();                                     // 释放相机
    cv::destroyAllWindows();                            // 销毁所有 OpenCV 窗口
    return 0;
}
