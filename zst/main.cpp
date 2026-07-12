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
#include <iostream>
#include <opencv2/highgui.hpp>
#include <string>

int main(int argc, char **argv)
{
    // 显式传参时优先使用用户给出的配置；未传参时根据可执行文件位置查找。
    // 因此无论从 build 目录、项目根目录还是 VS Code 启动，都不会依赖当前工作目录。
    const std::string configPath = argc > 1
        ? std::string(argv[1])
        : findDefaultConfigPath(argv[0]).string();

    std::cout << "[Main] 配置文件: " << configPath << std::endl;

    // 1. 读取配置。
    // 配置文件里放模型路径、相机参数、串口参数、是否显示窗口等。
    // 这些参数不写死在代码里，方便比赛现场快速改。
    AppConfig config;
    if (!loadAppConfig(configPath, config))
    {
        // 模型路径也来自配置文件，继续运行只会在加载 ONNX 时再次报错。
        std::cerr << "[Main] 配置文件加载失败，程序退出" << std::endl;
        return 1;
    }

    // 2. 打开工业相机。
    // MindVisionCamera 内部会：
    //   1) 初始化 MindVision SDK；
    //   2) 枚举相机；
    //   3) 打开指定 index 的相机；
    //   4) 设置 BGR8 输出，方便 OpenCV 使用。
    MindVisionCamera camera(config.camera);
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
    VisionSystem vision(config.vision);
    VirtualSerial serial(config.serial);
    if (!serial.openPort())
    {
        std::cerr << "[Main] 串口打开失败，发送时将尝试扫描设备并重连" << std::endl;
    }

    // 两队共用识别算法、协议编码和物理串口，只切换上层流程状态机。
    // TeamA：全部数字 -> 通知 -> 全部豆子 -> 通知 -> 完整结果。
    // TeamB：第1个豆子 -> 全部数字 -> 第1个匹配 -> 后续豆子逐个匹配。
    CompetitionWorkflow workflow(config.workflow);

    const std::string windowName = "Logistics Vision";
    if (config.showWindow) cv::namedWindow(windowName, cv::WINDOW_NORMAL);

    while (true)
    {
        // 4. 取图。
        // frame 是一帧 BGR 图像，后面会直接送进 YOLO。
        // 如果取图失败，说明相机超时或连接异常，本帧跳过。
        cv::Mat frame;
        if (!camera.read(frame) || frame.empty())
        {
            std::cerr << "[Main] 相机取图超时" << std::endl;
            continue;
        }

        // 5. YOLO 检测 + SVM 复核 + 任务规划。
        // result.detections：当前帧所有豆子/数字箱检测框；
        // result.decision：根据比赛规则算出的目标数字箱；
        // result.debugImage：画好框和文字的调试图。
        VisionFrameResult result = vision.process(frame);

        // 6. 当前队伍工作流决定“这一帧是否产生阶段消息”。
        // update只处理识别顺序和生成消息，不直接操作串口，因此TeamA/B流程可以独立测试。
        // 单向模式不等待电控 ACK；sendPacket内部只保证尝试把字节写入Linux串口，
        // 写成功并不能证明C板已经收到或完成运动。
        const std::vector<VisionTxPacket> packets = workflow.update(result.detections);
        for (const VisionTxPacket &tx : packets)
        {
            // TeamA豆子阶段完成时packets包含两帧：BeansComplete和FinalResult，
            // 这里按vector顺序逐帧发送，完整字节不会相互交叉。
            if (!serial.sendPacket(tx))
                std::cerr << "[Main] 工作流消息发送失败" << std::endl;
        }

        // 7. 调试窗口显示。
        // 按 ESC / q 退出程序。
        // 实车跑无屏幕模式时，可以在配置里关闭 show_window。
        if (config.showWindow)
        {
            cv::imshow(windowName, result.debugImage);
            int key = cv::waitKey(1);
            if (key == 27 || key == 'q' || key == 'Q') break;
            // 调试时按 1/2 可切换队伍。切换会清空旧识别缓存并开始新会话。
            if (key == '1') workflow.switchMode(TeamMode::TeamA);
            if (key == '2') workflow.switchMode(TeamMode::TeamB);
        }
    }

    serial.closePort();
    camera.close();
    cv::destroyAllWindows();
    return 0;
}
