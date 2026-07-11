/**
 * 实时视频目标检测 —— 豆子与数字识别 + 三厢位置判断 + 串口通信
 *
 * 使用 YOLOv8 ONNX 模型 + ONNX Runtime 进行实时推理，
 * 检测三厢豆子的左/中/右相对位置，通过串口发送给电控端，
 * 电控端根据位置+类别抓取豆子放入对应数字箱：
 *   黄豆→1号箱  绿豆→2号箱  白芸豆→3号箱
 *
 * 用法:
 *   bean_number_detector [配置文件] [视频源]
 *   配置文件默认 config.yaml，视频源默认 0（摄像头）
 */

#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>

#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <cmath>

#include "config.hpp"
#include "preprocess.hpp"
#include "detection.hpp"
#include "visualization.hpp"
#include "utils.hpp"
#include "industrial_camera.hpp"
#include "VirtualSerial.h"

// ============================================================
//  辅助函数：从检测结果中计算三厢豆子位置
// ============================================================

/**
 * @brief 基于豆子检测框中心 X 坐标，划分左/中/右三厢位置
 *
 * 策略：
 *   1. 只取豆子类别的检测 (classId 0/1/2)
 *   2. 将画面水平均分为 3 个区（左 / 中 / 右）
 *   3. 每个检测按其 centerX 落入对应区
 *   4. 若某区无豆子，用缺失的豆子类型自动补位
 *   5. 若某区有多个候选，保留置信度最高者
 *
 * @param dets       NMS 后的检测列表
 * @param frameWidth 画面宽度（像素）
 * @return 三个位置的豆子类别
 */
static BeanPositionResult computeBeanPositions(const std::vector<Detection>& dets,
                                                int frameWidth)
{
    BeanPositionResult result;

    // ---- 筛选豆子检测 ----
    std::vector<Detection> beans;
    for (const auto& d : dets) {
        if (d.classId == SOYBEAN_CLASS_ID ||
            d.classId == MUNG_BEAN_CLASS_ID ||
            d.classId == WHITE_KIDNEY_BEAN_CLASS_ID) {
            beans.push_back(d);
        }
    }

    // ★ 仅当检测到恰好 3 箱豆子时才计算左中右位置关系
    if (beans.size() != 3) return result;   // 全 0xFF

    // ---- 按 centerX 排序（左 → 右） ----
    std::sort(beans.begin(), beans.end(),
              [](const Detection& a, const Detection& b) {
                  float cxA = a.box.x + a.box.width  * 0.5f;
                  float cxB = b.box.x + b.box.width  * 0.5f;
                  return cxA < cxB;
              });

    const float zoneW = static_cast<float>(frameWidth) / 3.0f;
    bool typeSeen[3] = {false, false, false};    // soybean, mung, white_kidney
    int  assigned[3] = {-1, -1, -1};             // [left, mid, right] = index into beans

    // ---- 第一轮：按 zone 分配 ----
    for (size_t i = 0; i < beans.size(); ++i) {
        float cx = beans[i].box.x + beans[i].box.width * 0.5f;
        int zone = std::min(2, std::max(0, static_cast<int>(cx / zoneW)));

        if (assigned[zone] == -1) {
            assigned[zone] = static_cast<int>(i);
            typeSeen[beans[i].classId] = true;
        }
    }

    // ---- 第二轮：未分配豆子填入最近空 zone ----
    for (size_t i = 0; i < beans.size(); ++i) {
        bool alreadyPlaced = false;
        for (int z = 0; z < 3; ++z) {
            if (assigned[z] == static_cast<int>(i)) { alreadyPlaced = true; break; }
        }
        if (alreadyPlaced) continue;

        float cx = beans[i].box.x + beans[i].box.width * 0.5f;
        int bestZone = -1;
        float bestDist = 1e9f;
        for (int z = 0; z < 3; ++z) {
            if (assigned[z] == -1) {
                float zoneCx = (z + 0.5f) * zoneW;
                float dist = std::abs(cx - zoneCx);
                if (dist < bestDist) { bestDist = dist; bestZone = z; }
            }
        }
        if (bestZone >= 0) {
            assigned[bestZone] = static_cast<int>(i);
            typeSeen[beans[i].classId] = true;
        }
    }

    // ---- 第三轮：空 zone 用缺失豆子类型补位 ----
    // 缺失类型按顺序填充：黄豆(0) → 绿豆(1) → 白芸豆(2)
    for (int z = 0; z < 3; ++z) {
        if (assigned[z] == -1) {
            for (int t = 0; t < 3; ++t) {
                if (!typeSeen[t]) {
                    assigned[z] = 100 + t;  // 标记为"推断"
                    typeSeen[t] = true;
                    break;
                }
            }
        }
    }

    // ---- 输出 ----
    auto getBeanClass = [&](int idx) -> uint8_t {
        if (idx == -1) return 0xFF;
        if (idx >= 100) return static_cast<uint8_t>(idx - 100);  // 推断值
        return static_cast<uint8_t>(beans[idx].classId);
    };

    result.leftBean  = getBeanClass(assigned[0]);
    result.midBean   = getBeanClass(assigned[1]);
    result.rightBean = getBeanClass(assigned[2]);

    // ---- 计算各位置对应的数字箱号 ----
    auto& boxMap = Config::get().beanToBox;
    auto getBoxNumber = [&](uint8_t beanClass) -> uint8_t {
        if (beanClass <= 2 && beanClass < static_cast<uint8_t>(boxMap.size()))
            return static_cast<uint8_t>(boxMap[beanClass]);
        return 0;  // 无有效映射
    };
    result.leftNum  = getBoxNumber(result.leftBean);
    result.midNum   = getBoxNumber(result.midBean);
    result.rightNum = getBoxNumber(result.rightBean);

    return result;
}

// ============================================================
//  main
// ============================================================
int main(int argc, char** argv)
{
    // ---- 参数解析 ----
    std::string configPath  = "config.yaml";
    std::string videoSource = "0";   // 默认使用工业相机索引 0

    if (argc >= 2) configPath  = argv[1];
    if (argc >= 3) videoSource = argv[2];

    // ---- 加载 YAML 配置 ----
    Config::get().load(configPath);
    auto& cfg = Config::get();

    bool useIndustrialCamera = isIntegerString(videoSource);

    std::string modelPath  = cfg.modelPath;
    std::string serialPort = cfg.serialPort;
    auto& classNames = cfg.classNames;

    // ---- 初始化串口通信 ----
    VirtualSerial serial(serialPort);
    serial.SetTxLogEnabled(true);
    serial.SetBaudRate(cfg.serialBaudRate);
    serial.SetRetryIntervalUs(cfg.serialRetryIntervalUs);
    serial.SetReconnectParams(cfg.serialReconnectMaxWaitMs,
                              cfg.serialReconnectIntervalMs);
    serial.SetAutoReconnect(cfg.serialAutoReconnect);
    if (!serial.Open()) {
        std::cerr << "[WARN] 串口打开失败，将进入模拟模式（不发送）\n";
        serial.SetSimulated(true);
    } else {
        std::cout << "[INFO] 串口已打开: " << serialPort << std::endl;
    }

    // ---- 初始化 ONNX Runtime ----
    std::cout << "[INFO] 正在加载模型: " << modelPath << std::endl;

    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "BeanNumberDetector");
    Ort::SessionOptions sessionOptions;
    sessionOptions.SetIntraOpNumThreads(cfg.numThreads);
    sessionOptions.SetGraphOptimizationLevel(
        static_cast<GraphOptimizationLevel>(cfg.graphOptimizationLevel));

    std::cout << "[INFO] 推理后端: CPU, 线程数: " << cfg.numThreads << std::endl;

    Ort::Session session(env, modelPath.c_str(), sessionOptions);

    // 获取输入输出信息
    Ort::AllocatorWithDefaultOptions allocator;
    size_t numInputNodes = session.GetInputCount();
    size_t numOutputNodes = session.GetOutputCount();

    std::cout << "[INFO] 模型输入数量: " << numInputNodes << std::endl;
    std::cout << "[INFO] 模型输出数量: " << numOutputNodes << std::endl;

    // 获取输入名称
    std::vector<const char*> inputNames;
    std::vector<Ort::AllocatedStringPtr> inputNamePtrs;
    for (size_t i = 0; i < numInputNodes; ++i) {
        inputNamePtrs.push_back(session.GetInputNameAllocated(i, allocator));
        inputNames.push_back(inputNamePtrs.back().get());
        auto typeInfo = session.GetInputTypeInfo(i);
        auto tensorInfo = typeInfo.GetTensorTypeAndShapeInfo();
        auto shape = tensorInfo.GetShape();
        std::cout << "  输入[" << i << "] " << inputNames[i] << " shape: [";
        for (size_t j = 0; j < shape.size(); ++j) {
            std::cout << (j ? ", " : "") << shape[j];
        }
        std::cout << "]" << std::endl;
    }

    // 获取输出名称
    std::vector<const char*> outputNames;
    std::vector<Ort::AllocatedStringPtr> outputNamePtrs;
    for (size_t i = 0; i < numOutputNodes; ++i) {
        outputNamePtrs.push_back(session.GetOutputNameAllocated(i, allocator));
        outputNames.push_back(outputNamePtrs.back().get());
        auto typeInfo = session.GetOutputTypeInfo(i);
        auto tensorInfo = typeInfo.GetTensorTypeAndShapeInfo();
        auto shape = tensorInfo.GetShape();
        std::cout << "  输出[" << i << "] " << outputNames[i] << " shape: [";
        for (size_t j = 0; j < shape.size(); ++j) {
            std::cout << (j ? ", " : "") << shape[j];
        }
        std::cout << "]" << std::endl;
    }

    int numClasses = static_cast<int>(classNames.size());
    int numAnchors = 8400;  // 从模型输出 shape [1, 12, 8400] 获取

    // ---- 内存信息（用于创建输入 tensor） ----
    Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    // ---- 打开视频源 ----
    cv::VideoCapture cap;
    CameraCtx camCtx;

    if (useIndustrialCamera) {
        int camIndex = std::stoi(videoSource);
        std::cout << "[INFO] 使用工业相机, 索引: " << camIndex << std::endl;
        camCtx.frameTimeoutMs = cfg.cameraFrameTimeoutMs;
        if (!initIndustrialCamera(camIndex, camCtx)) {
            std::cerr << "[ERROR] 工业相机初始化失败" << std::endl;
            return -1;
        }
    } else {
        cap.open(videoSource);
        if (!cap.isOpened()) {
            std::cerr << "[ERROR] 无法打开视频源: " << videoSource << std::endl;
            return -1;
        }
        std::cout << "[INFO] 视频源已打开: " << videoSource << std::endl;
    }

    std::cout << "[INFO] 按 ESC 键退出\n";

    // ---- 颜色表 ----
    auto colors = buildColorTable(static_cast<int>(classNames.size()));

    // ---- FPS 统计 ----
    std::vector<double> latencyRing(30, 0.0);
    size_t ringIdx = 0;

    // ---- 简化状态：记录豆子位置，按右→中→左顺序匹配数字 ----
    bool     beansRecorded        = false;   // 是否已记录3箱豆子位置
    int      cyclePhase           = 0;       // 0=右, 1=中, 2=左
    uint8_t  targetRightNum       = 0;       // 右侧豆子对应箱号
    uint8_t  targetMidNum         = 0;       // 中间豆子对应箱号
    uint8_t  targetLeftNum        = 0;       // 左侧豆子对应箱号
    bool     waitingForBeanConfirm = false;  // 匹配成功后等待3箱豆子确认
    bool     matchingDone         = false;   // 三轮全部完成

    // ---- 主循环 ----
    cv::Mat frame;
    while (true) {
        bool gotFrame = false;

        if (useIndustrialCamera) {
            gotFrame = captureIndustrialFrame(camCtx, frame);
        } else {
            cap >> frame;
            gotFrame = !frame.empty();
        }

        if (!gotFrame) {
            if (useIndustrialCamera) {
                std::cerr << "[WARN] 工业相机取帧连续失败，退出" << std::endl;
            } else {
                std::cout << "[INFO] 视频结束\n";
            }
            break;
        }

        auto t0 = std::chrono::steady_clock::now();

        // 1. 预处理
        int   dw = 0, dh = 0;
        float scale = 1.0f;
        cv::Mat blob = preprocess(frame, dw, dh, scale);

        // 2. 前向推理 (ONNX Runtime)
        std::vector<int64_t> inputShape = {1, 3, cfg.inputHeight, cfg.inputWidth};
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memoryInfo,
            const_cast<float*>(blob.ptr<float>(0)),
            blob.total(),
            inputShape.data(),
            inputShape.size()
        );

        auto outputs = session.Run(Ort::RunOptions{nullptr},
                                   inputNames.data(), &inputTensor, 1,
                                   outputNames.data(), outputNames.size());

        // 3. 解析输出
        std::vector<Detection> dets;
        if (!outputs.empty()) {
            const float* outputData = outputs[0].GetTensorData<float>();
            dets = parseYOLOv8Output(outputData, numClasses, numAnchors,
                                     frame.cols, frame.rows,
                                     scale, dw, dh);
        }

        // 4. 计算三厢豆子位置 (左/中/右)
        BeanPositionResult pos = computeBeanPositions(dets, frame.cols);

        // 5. 简化逻辑：
        //    首次3箱豆子 → 记录位置 → 匹配右豆数字 → 发信号1
        //    再次3箱豆子 → 匹配中豆数字 → 发信号1
        //    再次3箱豆子 → 匹配左豆数字 → 发信号1
        //    三轮完成后不退出，仅不再匹配
        bool hasValidPosition = (pos.leftBean != 0xFF ||
                                 pos.midBean != 0xFF ||
                                 pos.rightBean != 0xFF);

        // ---- 5a. 首次检测到3箱豆子：记录位置 ----
        if (!beansRecorded && hasValidPosition) {
            targetRightNum = pos.rightNum;
            targetMidNum   = pos.midNum;
            targetLeftNum  = pos.leftNum;
            beansRecorded  = true;
            cyclePhase     = 0;
            waitingForBeanConfirm = false;

            std::cout << "[INFO] 豆子位置已记录"
                      << " | 右→箱" << static_cast<int>(targetRightNum)
                      << " | 中→箱" << static_cast<int>(targetMidNum)
                      << " | 左→箱" << static_cast<int>(targetLeftNum) << std::endl;
        }

        // ---- 5b. 匹配成功后的3箱确认：推进到下一阶段 ----
        if (waitingForBeanConfirm && hasValidPosition) {
            waitingForBeanConfirm = false;
            cyclePhase++;

            const char* phaseName[3] = {"右", "中", "左"};
            if (cyclePhase >= 3) {
                matchingDone = true;
                std::cout << "[INFO] 三轮匹配全部完成！" << std::endl;
            } else {
                std::cout << "[INFO] 3箱豆子确认，进入 Phase " << cyclePhase
                          << " (" << phaseName[cyclePhase] << ")" << std::endl;
            }
        }

        // ---- 5c. 数字匹配：找离画面中心最近的数字，与当前阶段目标比较 ----
        if (beansRecorded && !matchingDone && !waitingForBeanConfirm) {
            int bestDigit = -1;
            float bestDist = 1e9f;
            float frameCx = frame.cols * 0.5f;
            float frameCy = frame.rows * 0.5f;

            for (const auto& d : dets) {
                if (d.classId >= 3 && d.classId <= 7) {
                    int digit = d.classId - 2;
                    float cx = d.box.x + d.box.width * 0.5f;
                    float cy = d.box.y + d.box.height * 0.5f;
                    float dist = std::sqrt((cx - frameCx) * (cx - frameCx) +
                                           (cy - frameCy) * (cy - frameCy));
                    if (dist < bestDist) {
                        bestDist = dist;
                        bestDigit = digit;
                    }
                }
            }

            if (bestDigit >= 1 && bestDigit <= 5) {
                uint8_t targetNum = 0;
                const char* phaseName = "";
                switch (cyclePhase) {
                    case 0: targetNum = targetRightNum; phaseName = "右"; break;
                    case 1: targetNum = targetMidNum;   phaseName = "中"; break;
                    case 2: targetNum = targetLeftNum;  phaseName = "左"; break;
                }

                if (static_cast<uint8_t>(bestDigit) == targetNum) {
                    serial.sendMatchSignal(1, cfg.serialMaxRetries);
                    waitingForBeanConfirm = true;

                    std::cout << "[INFO] 匹配！Phase " << cyclePhase
                              << " (" << phaseName << ")"
                              << " 目标=" << static_cast<int>(targetNum)
                              << " 检测=" << bestDigit
                              << " — 等待3箱豆子确认..." << std::endl;
                }
            }
        }

        // 6. 统计
        int beanCount = 0, numberCount = 0;
        for (const auto& d : dets) {
            if (d.classId == SOYBEAN_CLASS_ID ||
                d.classId == MUNG_BEAN_CLASS_ID ||
                d.classId == WHITE_KIDNEY_BEAN_CLASS_ID) {
                ++beanCount;
            } else {
                ++numberCount;
            }
        }

        // 7. 绘制
        drawDetections(frame, dets, colors, classNames, cfg);
        drawBeanPositions(frame, pos, cfg);   // 叠加左/中/右位置信息

        auto t1 = std::chrono::steady_clock::now();
        double latencyMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

        latencyRing[ringIdx % latencyRing.size()] = latencyMs;
        ++ringIdx;
        double avgLatency = (ringIdx >= latencyRing.size())
            ? std::accumulate(latencyRing.begin(), latencyRing.end(), 0.0) / latencyRing.size()
            : latencyMs;
        double fps = (avgLatency > 0.0) ? 1000.0 / avgLatency : 0.0;

        drawStats(frame, beanCount, numberCount, fps, avgLatency);

        // 8. 显示
        cv::imshow("Bean & Number Detector (ESC to quit)", frame);

        int key = cv::waitKey(1) & 0xFF;
        if (key == 27) {  // ESC
            std::cout << "[INFO] 用户退出\n";
            break;
        }
    }

    if (useIndustrialCamera) {
        releaseIndustrialCamera(camCtx);
    } else {
        cap.release();
    }
    cv::destroyAllWindows();
    std::cout << "[INFO] 程序结束\n";
    return 0;
}
