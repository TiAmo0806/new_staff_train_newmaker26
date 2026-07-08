/**
 * @file main.cpp
 * @brief 起重机物流视觉主程序
 *   相机相关问题 -> CameraDriver/
 *   YOLO/SVM/规则判断 -> ImgProcessing/
 *   串口协议 -> Communication/
 *   配置读取 -> Tool/
 */

#include "/home/zst/zst/include/CameraDriver/MindVisionCamera.h"
#include "/home/zst/zst/include/Communication/VirtualSerial.h"
#include "/home/zst/zst/include/ImgProcessing/VisionSystem.h"
#include "/home/zst/zst/include/Tool/Utils.h"
#include <iostream>
#include <opencv2/highgui.hpp>
#include <string>

int main(int argc, char **argv)
{
    // 默认从 build 目录运行，所以配置路径指向 ../config。
    // 如果你在其他位置运行，也可以手动传入配置文件：
    //   ./logistics_vision /home/zst/src/config/vision.yaml
    std::string configPath = argc > 1 ? argv[1] : "../config/vision.yaml";

    // 1. 读取配置。
    // 配置文件里放模型路径、相机参数、串口参数、是否显示窗口等。
    // 这些参数不写死在代码里，方便比赛现场快速改。
    AppConfig config;
    if (!loadAppConfig(configPath, config))
    {
        std::cerr << "[Main] 使用默认配置继续运行" << std::endl;
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
    // VirtualSerial 目前只占住通信流程。
    // 电控协议字段没定之前，payload 保持为空。
    VisionSystem vision(config.vision);
    VirtualSerial serial(config.serial);
    if (!serial.openPort())
    {
        std::cerr << "[Main] 串口打开失败，切换模拟发送" << std::endl;
    }

    cv::VideoWriter writer;
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

        // 6. 串口发送。
        // 电控协议未定：这里先发送空 payload，占住通信流程。
        // 后续协议确定后，在这里把 result.decision 转成电控需要的字节。
        //
        // 例如以后可能会发：
        //   是否识别到目标
        //   目标箱编号 1/2/3
        //   目标中心相对图像中心的偏差
        //   当前工作状态
        VisionTxPacket tx;
        serial.sendPacket(tx);

        // 7. 调试窗口显示。
        // 按 ESC / q 退出程序。
        // 实车跑无屏幕模式时，可以在配置里关闭 show_window。
        if (config.showWindow)
        {
            cv::imshow(windowName, result.debugImage);
            int key = cv::waitKey(1);
            if (key == 27 || key == 'q' || key == 'Q') break;
        }
    }

    serial.closePort();
    camera.close();
    cv::destroyAllWindows();
    return 0;
}
