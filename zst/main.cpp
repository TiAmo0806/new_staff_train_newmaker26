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
#include <opencv2/imgproc.hpp>
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
    if (!camera.open())
    {
        std::cerr << "[Main] 相机打开失败" << std::endl;
        return 1;
    }

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
    // TeamA：全部数字 -> 通知 -> 全部豆子 -> 通知 -> 完整结果。
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
              << "，十六进制日志=" << (config.serial.txLog ? "开启" : "关闭") << std::endl;

    const std::string windowName = "Logistics Vision";
    if (config.showWindow) cv::namedWindow(windowName, cv::WINDOW_NORMAL); // 创建可缩放窗口

    using Clock = std::chrono::steady_clock;
    auto statsWindowStart = Clock::now();
    auto previousFrameEnd = statsWindowStart;
    int statsFrameCount = 0;
    int consecutiveCaptureFailures = 0;
    double captureMsSum = 0.0;
    double visionMsSum = 0.0;
    double displayedFps = 0.0;                         // 指数平滑后的实时FPS
    cv::Mat lastDebugImage;                            // 相机短暂掉线时保持窗口可响应

    while (true)
    {
        // 4. 取图。
        // frame 是一帧 BGR 图像，后面会直接送进 YOLO。
        // 如果连续取图失败，自动重开相机；失败期间仍调用waitKey，避免窗口假死。
        const auto captureStart = Clock::now();
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
                const int key = cv::waitKey(1);
                if (key == 27 || key == 'q' || key == 'Q') break;
            }

            if (consecutiveCaptureFailures >= config.camera.reconnectAfterFailures)
            {
                std::cerr << "[CameraWatchdog] 尝试重新打开相机" << std::endl;
                camera.close();
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                if (camera.open())
                {
                    std::cout << "[CameraWatchdog] 相机重连成功" << std::endl;
                    consecutiveCaptureFailures = 0;
                    const auto recoveredAt = Clock::now();
                    previousFrameEnd = recoveredAt;      // 排除重连时间对FPS平滑值的影响
                    statsWindowStart = recoveredAt;
                    statsFrameCount = 0;
                    captureMsSum = 0.0;
                    visionMsSum = 0.0;
                    displayedFps = 0.0;
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
        const auto captureEnd = Clock::now();
        const double captureMs =
            std::chrono::duration<double, std::milli>(captureEnd - captureStart).count();
        consecutiveCaptureFailures = 0;

        // 5. YOLO 检测 + SVM 复核 + 任务规划。
        // result.detections：当前帧所有豆子/数字箱检测框；
        // result.decision：根据比赛规则算出的目标数字箱；
        // result.debugImage：画好框和文字的调试图。
        const auto visionStart = Clock::now();
        VisionFrameResult result = vision.process(frame); // 完整一帧视觉处理
        const auto visionEnd = Clock::now();
        const double visionMs =
            std::chrono::duration<double, std::milli>(visionEnd - visionStart).count();

        // 6. 当前队伍工作流决定"这一帧是否产生阶段消息"。
        // update只处理识别顺序和生成消息，不直接操作串口，因此TeamA/B流程可以独立测试。
        // 单向模式不等待电控 ACK；sendPacket内部只保证尝试把字节写入Linux串口，
        // 写成功并不能证明C板已经收到或完成运动。
        // imageWidth用于B组选择最靠近画面中心的豆子；A组仍按原来的从左到右规则。
        const std::vector<VisionTxPacket> packets =
            workflow.update(result.detections, frame.cols); // 工作流更新
        for (const VisionTxPacket &tx : packets)
        {
            // TeamA豆子阶段完成时packets包含两帧：BeansComplete和FinalResult，
            // 这里按vector顺序逐帧发送，完整字节不会相互交叉。
            if (!serial.sendPacket(tx))                 // 封帧 + CRC + 写入串口
                std::cerr << "[Main] 工作流消息发送失败" << std::endl;
        }

        // 以完整处理帧的间隔计算实时FPS，再用指数平滑减少数字跳动。
        const auto frameEnd = Clock::now();
        const double frameInterval =
            std::chrono::duration<double>(frameEnd - previousFrameEnd).count();
        previousFrameEnd = frameEnd;
        if (frameInterval > 0.0 && frameInterval < 2.0)
        {
            const double instantaneousFps = 1.0 / frameInterval;
            displayedFps = displayedFps <= 0.0
                ? instantaneousFps
                : displayedFps * 0.90 + instantaneousFps * 0.10;
        }

        statsFrameCount++;
        captureMsSum += captureMs;
        visionMsSum += visionMs;
        const double statsSeconds =
            std::chrono::duration<double>(frameEnd - statsWindowStart).count();
        if (statsSeconds >= 1.0)
        {
            const double windowFps = statsFrameCount / statsSeconds;
            std::cout << "[Performance] FPS=" << windowFps
                      << "，capture=" << (captureMsSum / statsFrameCount) << "ms"
                      << "，vision=" << (visionMsSum / statsFrameCount) << "ms"
                      << std::endl;
            statsWindowStart = frameEnd;
            statsFrameCount = 0;
            captureMsSum = 0.0;
            visionMsSum = 0.0;
        }

        // 在画面左上角第二行显示端到端FPS、取帧耗时和视觉推理耗时。
        const std::string performanceText = cv::format(
            "FPS %.1f | CAP %.1f ms | VISION %.1f ms",
            displayedFps, captureMs, visionMs);
        cv::putText(result.debugImage, performanceText, cv::Point(20, 75),
                    cv::FONT_HERSHEY_SIMPLEX, 0.65, cv::Scalar(255, 255, 0), 2);
        lastDebugImage = result.debugImage;

        // 7. 调试窗口显示。
        // 按 ESC / q 退出程序。
        // 实车跑无屏幕模式时，可以在配置里关闭 show_window。
        if (config.showWindow)
        {
            cv::imshow(windowName, result.debugImage);  // 显示调试图
            int key = cv::waitKey(1);                   // 等待 1ms，获取按键
            if (key == 27 || key == 'q' || key == 'Q') break; // ESC 或 q 退出
            // 调试时按 1/2 可切换队伍。切换会清空旧识别缓存并开始新会话。
            if (key == '1') workflow.switchMode(TeamMode::TeamA); // 切换到队伍A
            if (key == '2') workflow.switchMode(TeamMode::TeamB); // 切换到队伍B
        }
    }

    serial.closePort();                                 // 关闭串口
    camera.close();                                     // 释放相机
    cv::destroyAllWindows();                            // 销毁所有 OpenCV 窗口
    return 0;
}
