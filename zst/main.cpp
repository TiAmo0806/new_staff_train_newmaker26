/**
 * @file main.cpp
 * @brief 起重机物流视觉主程序
 *   相机相关问题 -> CameraDriver/
 *   YOLO/SVM/规则判断 -> ImgProcessing/
 *   串口协议 -> Communication/
 *   配置读取 -> Tool/
 */

#include "CameraDriver/MindVisionCamera.h"
#include "Communication/VisionProtocol.h"
#include "ImgProcessing/CompetitionWorkflow.h"
#include "Communication/VirtualSerial.h"
#include "ImgProcessing/VisionSystem.h"
#include "Tool/Utils.h"
#include <chrono>
#include <iostream>
#include <opencv2/highgui.hpp>
#include <string>
#include <thread>

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
        // 模型路径也来自配置文件，继续运行只会在加载 ONNX 时再次报错。
        std::cerr << "[Main] 配置文件加载失败，程序退出" << std::endl;
        return 1;                                       // 非零退出码表示异常
    }

    // 2. 打开工业相机。
    // MindVisionCamera 内部会：
    //   1) 初始化 MindVision SDK；
    //   2) 枚举相机；
    //   3) 打开指定 index 的相机；
    //   4) 设置 BGR8 输出，方便 OpenCV 使用。
    MindVisionCamera camera(config.camera);             // 用配置创建相机对象
    // 相机第一次打开失败时不再退出整个程序，因为串口必须继续运行，才能等待电控
    // 后续发送camera_state=1。cameraIsOpen只表示SDK当前是否真正持有相机。
    bool cameraIsOpen = camera.open();
    if (!cameraIsOpen)
        std::cerr << "[Main] 相机首次打开失败，程序保留运行并等待重连/电控开启命令"
                  << std::endl;

    // 默认保持原来的“程序启动即开相机”行为，兼容电控尚未发送控制帧的调试场景。
    // 收到camera_state=0后变为false，此时停止取图和推理，但串口主循环继续运行。
    bool cameraEnabled = true;

    // 3. 初始化视觉系统和串口。
    // VisionSystem 内部包含：
    //   YOLO ONNX Runtime 检测；
    //   SVM 豆子二次分类；
    //   TaskPlanner 比赛规则决策。
    //
    // VirtualSerial 负责统一的可变长度封帧、CRC 和 Linux 串口写入。
    VisionSystem vision(config.vision);                 // 创建视觉系统
    VirtualSerial serial(config.serial);                // 创建虚拟串口
    if (!serial.openPort())
    {
        std::cerr << "[Main] 串口打开失败，发送时将尝试扫描设备并重连" << std::endl;
    }

    // 两队共用识别算法、协议编码和物理串口，只切换上层流程状态机。
    // TeamA：全部数字 -> 发送数字数组 -> 全部豆子 -> 发送豆子数组。
    // 每次成功发送后保存断点，程序重启后会从下一阶段继续，不会重新等待数字。
    // TeamB：中心第1个豆子 -> 全部数字place1~5 -> 中心剩余新豆子；类型顺序不固定。
    CompetitionWorkflow workflow(config.workflow);      // 创建比赛工作流
    std::cout << "[Main] 当前队伍: " << teamModeToString(config.workflow.mode)
              << "，投票帧数=" << config.workflow.voteFramesPerStage
              << "，最少命中=" << config.workflow.minHitsPerStage
              << "，B组中心区域宽度="
              << (config.workflow.teamBCenterWidthRatio * 100.0f) << "%" << std::endl;
    std::cout << "[Main] 串口模式: " << (config.serial.simulated ? "模拟发送" : "真实串口")
              << "，端口=" << config.serial.port
              << "，波特率=" << config.serial.baudrate
              << "，TX日志=" << (config.serial.txLog ? "开启" : "关闭")
              << "，RX日志=" << (config.serial.rxLog ? "开启" : "关闭") << std::endl;

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

    while (true)
    {
        // 4. 先处理电控->视觉的相机控制帧，再决定本轮是否取图。
        // receiveCameraState()为非阻塞函数；当前没有完整帧时会立即返回false，
        // 不会因为等待串口而拖慢相机帧率。while用于一次处理串口中积压的多条命令，
        // 最后一条合法命令决定最终状态，例如连续收到0、1后保持开启。
        uint8_t requestedCameraState = 0;
        while (serial.receiveCameraState(requestedCameraState))
        {
            const bool requestedEnabled = requestedCameraState == 1;
            if (requestedEnabled == cameraEnabled) continue; // 重复命令无需反复开关SDK

            cameraEnabled = requestedEnabled;
            if (!cameraEnabled)
            {
                // 只关闭相机SDK，不退出程序、不关闭串口，也不清空A/B识别和断点数据。
                // 因此A组发完数字后收到0，之后再收到1仍会直接进入豆子阶段。
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

        // 6. YOLO 检测 + SVM 复核 + 任务规划。
        // result.detections：当前帧所有豆子/数字箱检测框；
        // result.decision：根据比赛规则算出的目标数字箱；
        // result.debugImage：画好框和文字的调试图。
        VisionFrameResult result = vision.process(frame); // 完整一帧视觉处理

        // 7. 当前队伍工作流决定"这一帧是否产生阶段消息"。
        // update只处理识别顺序和生成消息，不直接操作串口，因此TeamA/B流程可以独立测试。
        // 当前已实现双向串口，但视觉结果发送仍不等待电控ACK；电控->视觉方向目前
        // 只接收camera_state。sendPacket写成功仍不能证明C板已经处理了视觉结果。
        // imageWidth用于B组选择最靠近画面中心的豆子；A组仍按原来的从左到右规则。
        const std::vector<VisionTxPacket> packets =
            workflow.update(result.detections, frame.cols); // 工作流更新
        for (const VisionTxPacket &tx : packets)
        {
            // 工作流可能在某一帧生成一条或多条消息；按vector顺序逐条发送，
            // 每条消息都会独立添加帧头和CRC，线路字节不会相互交叉。
            if (!serial.sendPacket(tx))                 // 封帧 + CRC + 写入串口
                std::cerr << "[Main] 工作流消息发送失败" << std::endl;
            else
                // 只有Linux串口层确认完整帧写入成功后才落盘断点。
                // 这样退出程序再启动时，会从“已经成功发送”的下一阶段继续。
                workflow.confirmPacketSent(tx);
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
