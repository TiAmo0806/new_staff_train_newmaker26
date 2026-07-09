/**
 * @file VisionNode.cpp
 * @brief 视觉节点 - 迈德威视相机实时画面 + ONNX 豆子识别 + 串口通信
 * @author lxy
 * @date 2026-06-27
 */

#include "BeanProtocol.hpp"
#include "SerialPort.h"
#include "CameraDriver.hpp"

#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <array>
#include <vector>
#include <algorithm>
#include <mutex>
#include <atomic>
#include <memory>
#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>

namespace bean_sorter
{
// 1. 配置结构体
struct Config
{
    std::string serialPort = "/dev/ttyACM0";
    std::string modelPath  = "model/best.onnx";
    bool        simulate   = false;
    bool        txLog      = true;
    bool        rxLog      = false;
    int         cameraIndex = 0;
    bool        showWindow = true;
};

// 2. 检测结果结构体
struct DetectionResult
{
    BeanType    type;
    uint8_t     confidence;
    float       center_x;
    float       center_y;
    float       size;
};

// 3. VisionNode 类

class VisionNode
{
public:
    VisionNode();
    ~VisionNode();

    bool Init(int argc, char * argv[]);
    int  Run();
    void Stop();

private:
    bool InitOnnx();

    static std::vector<float> Preprocess(
        const cv::Mat & frame, int targetW, int targetH,
        float & scale, int & padX, int & padY);

    static DetectionResult ParseOutput(
        const float * output, int numDetections, int stride,
        float confThreshold, int targetW, int targetH,
        int frameW, int frameH);

    static std::vector<DetectionResult> Nms(
        const std::vector<DetectionResult> & dets,
        float iouThreshold, int frameW, int frameH);

    std::vector<DetectionResult> ParseRawYolo(//把 8400 个网格的原始数据，筛选出置信度高的目标，转换成坐标信息
        const float * data, int channels, int numGrid,
        float confThreshold, int targetW, int targetH,
        int frameW, int frameH,
        float letterScale = 1.0f, int padX = 0, int padY = 0);

    std::vector<DetectionResult> DetectBeans(const cv::Mat & frame);

    static void DrawResult(cv::Mat & frame, const DetectionResult & det);

    void InferenceLoop();//后台推理线程
    void SendDetection(const DetectionResult & det);

    Config                      config_;
    std::unique_ptr<SerialPort> serial_;
    //推理模型有关成员变量
    Ort::Env                    onnxEnv_;//使用onnxruntime
    Ort::SessionOptions         sessionOpts_;
    Ort::Session                session_;
    std::array<int64_t, 4>      inputShape_;//用于存储 ONNX 模型的输入张量形状

    CameraDriver                camera_;
   //多线程有关成员变量
    std::thread                 infThread_;
    std::vector<DetectionResult> lastDets_;
    cv::Mat                     sharedFrame_;
    std::mutex                  detsMtx_;//多线程推理时用于保护检测结果
    std::mutex                  frameMtx_;//保护共享帧
    std::atomic<bool>           systemRunning_;//控制线程运行或停止

    int totalCount_;
    int sendCount_;
    int busyCount_;
};

// 4. 构造函数 / 析构函数
VisionNode::VisionNode()
    : serial_(nullptr)
    , onnxEnv_(nullptr)
    , session_(nullptr)
    , systemRunning_(false)
    , totalCount_(0)
    , sendCount_(0)
    , busyCount_(0)
{
}

VisionNode::~VisionNode()
{
    Stop();
}

//5.初始化
bool VisionNode::Init(int argc, char * argv[])
{
    Config cfg;
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--simulate")        cfg.simulate = true;
        else if (arg == "--txlog")      cfg.txLog = true;
        else if (arg == "--rxlog")      cfg.rxLog = true;
        else if (arg == "--no-window")  cfg.showWindow = false;
        else if (arg[0] != '-')         cfg.serialPort = arg;
    }
    config_ = cfg;//把命令行解析出来的配置参数，保存到对象的成员变量 config_ 中。

    if (!config_.simulate)
    {
        if (!InitOnnx())
        {
            std::cerr << "[视觉] 模型加载失败，降级到模拟模式" << std::endl;
            config_.simulate = true;
        }
    }

    serial_ = std::make_unique<SerialPort>(config_.serialPort);
    serial_->SetSimulated(config_.simulate);
    serial_->SetTxLogEnabled(config_.txLog);
    serial_->SetRxLogEnabled(config_.rxLog);

    if (!config_.simulate)
    {
        if (!serial_->Open())
        {
            std::cerr << "[视觉] 打开串口失败 " << config_.serialPort << std::endl;
            serial_->SetSimulated(true);
            std::cout << "[视觉] 降级到模拟模式" << std::endl;
        }
    }

    if (!config_.simulate)
    {
        if (!camera_.Open(config_.cameraIndex))
        {
            std::cerr << "[视觉] 打开相机失败，使用模拟模式" << std::endl;
            config_.simulate = true;
        }
    }

    std::cout << "\n========== 抓豆分拣 视觉节点 v2.0 ==========" << std::endl;
    std::cout << "  串口: " << serial_->GetPortName()
              << " | 模式: " << (config_.simulate ? "模拟" : "真实相机+串口") << std::endl;
    if (!config_.simulate)
        std::cout << "  相机: " << camera_.GetCameraName() << std::endl;
    std::cout << "=============================================\n" << std::endl;

    return true;
}

//6.加载onnx 模型

bool VisionNode::InitOnnx()
{
    onnxEnv_ = Ort::Env(OrtLoggingLevel::ORT_LOGGING_LEVEL_WARNING, "bean_sorter");
    sessionOpts_.SetIntraOpNumThreads(4);
    sessionOpts_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    session_ = Ort::Session(onnxEnv_, config_.modelPath.c_str(), sessionOpts_);

    auto inputTypeInfo = session_.GetInputTypeInfo(0);
    auto inputTensorInfo = inputTypeInfo.GetTensorTypeAndShapeInfo();
    auto shapeVec = inputTensorInfo.GetShape();
    for (int i = 0; i < 4 && i < static_cast<int>(shapeVec.size()); ++i)
        inputShape_[i] = shapeVec[i];

    std::cout << "[视觉] 模型加载: " << config_.modelPath << std::endl;
    std::cout << "[视觉] 输入形状: N=" << inputShape_[0]
              << " C=" << inputShape_[1]
              << " H=" << inputShape_[2]
              << " W=" << inputShape_[3] << std::endl;

    return true;
}

// 7. Preprocess() - 图像预处理

std::vector<float> VisionNode::Preprocess(
    const cv::Mat & frame, int targetW, int targetH,
    float & scale, int & padX, int & padY)
{//计算如何保持宽高比地将图像缩放到目标尺寸
    float s = std::min(
        static_cast<float>(targetW) / frame.cols,
        static_cast<float>(targetH) / frame.rows);
    int newW = static_cast<int>(frame.cols * s);
    int newH = static_cast<int>(frame.rows * s);
    int dx = (targetW - newW) / 2;
    int dy = (targetH - newH) / 2;

    cv::Mat resized;
    cv::resize(frame, resized, cv::Size(newW, newH));

    cv::Mat canvas(targetH, targetW, CV_8UC3, cv::Scalar(114, 114, 114));
    resized.copyTo(canvas(cv::Rect(dx, dy, newW, newH)));

    cv::Mat rgb, blob;
    cv::cvtColor(canvas, rgb, cv::COLOR_BGR2RGB);
    rgb.convertTo(blob, CV_32FC3, 1.0f / 255.0f);//归一化
    //把图像数据转换成ONNX 模型要求的输入格式
    std::vector<float> input(targetW * targetH * 3);
    float * dst = input.data();
    for (int c = 0; c < 3; ++c)
        for (int i = 0; i < targetH * targetW; ++i)
            dst[c * targetH * targetW + i] = blob.at<cv::Vec3f>(i)[c];

    scale = s;
    padX  = dx;
    padY  = dy;
    return input;
}

// 8. ParseOutput() - 解析 NMS 格式输出

DetectionResult VisionNode::ParseOutput(
    const float * output, int numDetections, int stride,
    float confThreshold, int targetW, int targetH,
    int frameW, int frameH)
{
    DetectionResult result{};
    result.type = BEAN_NONE;

    float bestConf = confThreshold;
    for (int i = 0; i < numDetections; ++i)
    {
        const float * row = output + i * stride;
        float cx   = row[0];
        float cy   = row[1];
        float w    = row[2];
        float h    = row[3];
        float conf = row[4];
        int classId = static_cast<int>(row[5]);

        if (conf < bestConf) continue;
        if (classId < 0 || classId > 2) continue;

        bestConf = conf;

        static int dbgOut = 0;
        if (dbgOut < 10)
        {
            std::cout << "[视觉][DBG] NMS格式 classId=" << classId
                      << " conf=" << conf << std::endl;
            dbgOut++;
        }

        float scaleX = static_cast<float>(frameW) / targetW;
        float scaleY = static_cast<float>(frameH) / targetH;

        float absCx = cx * scaleX;
        float absCy = cy * scaleY;
        float absW  = w  * scaleX;

        result.type       = static_cast<BeanType>(classId + 1);
        result.confidence = static_cast<uint8_t>(std::min(99.0f, conf * 100.0f));
        result.center_x   = absCx / frameW * 2.0f - 1.0f;
        result.center_y   = absCy / frameH * 2.0f - 1.0f;
        result.size       = absW / frameW;
    }

    return result;
}

// 9. Nms() - 非极大值抑制

std::vector<DetectionResult> VisionNode::Nms(
    const std::vector<DetectionResult> & dets,
    float iouThreshold, int frameW, int frameH)
{
    if (dets.empty()) return {};

    std::vector<int> idx(dets.size());
    for (size_t i = 0; i < dets.size(); ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](int a, int b)
    {
        return dets[a].confidence > dets[b].confidence;
    });

    std::vector<bool> removed(dets.size(), false);
    std::vector<DetectionResult> kept;

    for (size_t i = 0; i < idx.size(); ++i)
    {
        if (removed[idx[i]]) continue;
        const auto & a = dets[idx[i]];
        kept.push_back(a);

        float ax1 = (a.center_x + 1.0f) * frameW / 2.0f - a.size * frameW / 2.0f;
        float ay1 = (a.center_y + 1.0f) * frameH / 2.0f - a.size * frameH / 2.0f;
        float ax2 = ax1 + a.size * frameW;
        float ay2 = ay1 + a.size * frameH;

        for (size_t j = i + 1; j < idx.size(); ++j)
        {
            if (removed[idx[j]]) continue;
            const auto & b = dets[idx[j]];

            float bx1 = (b.center_x + 1.0f) * frameW / 2.0f - b.size * frameW / 2.0f;
            float by1 = (b.center_y + 1.0f) * frameH / 2.0f - b.size * frameH / 2.0f;
            float bx2 = bx1 + b.size * frameW;
            float by2 = by1 + b.size * frameH;

            float interX1 = std::max(ax1, bx1);
            float interY1 = std::max(ay1, by1);
            float interX2 = std::min(ax2, bx2);
            float interY2 = std::min(ay2, by2);
            float inter = std::max(0.0f, interX2 - interX1)
                        * std::max(0.0f, interY2 - interY1);
            float areaA = (ax2 - ax1) * (ay2 - ay1);
            float areaB = (bx2 - bx1) * (by2 - by1);
            float iou   = inter / (areaA + areaB - inter);

            if (iou > iouThreshold) removed[idx[j]] = true;
        }
    }
    return kept;
}

// 10. ParseRawYolo() - 解析 YOLO 原始输出

std::vector<DetectionResult> VisionNode::ParseRawYolo(
    const float * data, int channels, int numGrid,
    float confThreshold, int targetW, int targetH,
    int frameW, int frameH,
    float letterScale, int padX, int padY)
{
    std::vector<DetectionResult> results;

    // 每帧统计所有类别的最高分
    {
        float maxScores[8] = {};
        for (int gi = 0; gi < numGrid; ++gi)
        {
            for (int c = 4; c < channels; ++c)
            {
                float s = data[gi + c * numGrid];
                if (s > maxScores[c - 4]) maxScores[c - 4] = s;
            }
        }
        static int frameCnt = 0;
        if (++frameCnt % 10 == 1)
        {
            std::cout << "[视觉] 各类别最高分:";
            for (int c = 0; c < channels - 4; ++c)
                std::cout << " " << c << "=" << maxScores[c];
            std::cout << std::endl;
        }
    }

    for (int i = 0; i < numGrid; ++i)
    {
        float cx = data[i + 0 * numGrid];
        float cy = data[i + 1 * numGrid];
        float w  = data[i + 2 * numGrid];
        float h  = data[i + 3 * numGrid];

        float maxScore = 0;
        int bestClass = -1;
        for (int c = 4; c < channels; ++c)
        {
            float score = data[i + c * numGrid];
            if (score > maxScore)
            {
                maxScore  = score;
                bestClass = c - 4;
            }
        }

        if (maxScore < confThreshold) continue;

        DetectionResult det{};
        det.type       = static_cast<BeanType>(bestClass + 1);
        det.confidence = static_cast<uint8_t>(std::min(99.0f, maxScore * 100.0f));

        float absCx = (cx - padX) / letterScale;
        float absCy = (cy - padY) / letterScale;
        float absW  = w / letterScale;
        float absH  = h / letterScale;

        det.center_x = (absCx / frameW) * 2.0f - 1.0f;
        det.center_y = (absCy / frameH) * 2.0f - 1.0f;
        det.size     = ((absW + absH) / 2.0f) / frameW;

        results.push_back(det);
    }
//------------------------------------------------------
    //调整NMS
//-------------------------------------------------------
    auto kept = Nms(results, 0.5f, frameW, frameH);

    static int frameCnt = 0;
    if (++frameCnt <= 5 && !kept.empty())
    {
        std::cout << "[视觉] 检测到 " << kept.size() << " 个:";
        for (auto & d : kept)
            std::cout << " cls=" << (static_cast<int>(d.type) - 1)
                      << "(" << static_cast<int>(d.confidence) << "%)";
        std::cout << std::endl;
    }
    return kept;
}

// 11. DetectBeans() - 推理入口

std::vector<DetectionResult> VisionNode::DetectBeans(const cv::Mat & frame)
{
    if (frame.empty()) return {};

    int targetW = static_cast<int>(inputShape_[3]);
    int targetH = static_cast<int>(inputShape_[2]);

    float letterScale = 1.0f;
    int padX = 0, padY = 0;
    auto input = Preprocess(frame, targetW, targetH, letterScale, padX, padY);

    Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(
        OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
        memInfo, input.data(), input.size(),
        inputShape_.data(), inputShape_.size());

    Ort::AllocatorWithDefaultOptions allocator;
    auto inputNamePtr  = session_.GetInputNameAllocated(0, allocator);
    auto outputNamePtr = session_.GetOutputNameAllocated(0, allocator);
    const char * inputNames[]  = { inputNamePtr.get() };
    const char * outputNames[] = { outputNamePtr.get() };

    auto outputTensors = session_.Run(
        Ort::RunOptions{nullptr},
        inputNames, &inputTensor, 1,
        outputNames, 1);

    auto outputInfo  = outputTensors[0].GetTensorTypeAndShapeInfo();
    auto outputShape = outputInfo.GetShape();

    size_t dataLen = outputInfo.GetElementCount();
    std::vector<float> outputCopy(dataLen);
    memcpy(outputCopy.data(), outputTensors[0].GetTensorData<float>(),
           dataLen * sizeof(float));
    const float * outputData = outputCopy.data();

    static bool shapeLogged = false;
    if (!shapeLogged && outputShape.size() >= 3)
    {
        int ch = static_cast<int>(outputShape.size() >= 2 ? outputShape[1] : 0);
        int gr = static_cast<int>(outputShape.size() >= 3 ? outputShape[2] : 0);
        std::cout << "[视觉] 输出形状: " << outputShape[0]
                  << "x" << ch << "x" << gr << std::endl;
        std::cout << "[视觉] 前12个浮点值:";
        for (int i = 0; i < 12 && i < static_cast<int>(dataLen); ++i)
            std::cout << " " << outputData[i];
        std::cout << std::endl;
        std::cout << "[视觉] grid[0] 全部12通道:";
        for (int c = 0; c < 12; ++c)
            std::cout << " ch" << c << "=" << outputData[0 + c * gr];
        std::cout << std::endl;
        shapeLogged = true;
    }

    if (outputShape.size() == 3)
    {
        if (outputShape[1] == 6)
        {
            return { ParseOutput(outputData,
                        static_cast<int>(outputShape[2]), 6, 0.5f,
                        targetW, targetH, frame.cols, frame.rows) };
        }
        else if (outputShape[2] == 6)
        {
            return { ParseOutput(outputData,
                        static_cast<int>(outputShape[1]), 6, 0.5f,
                        targetW, targetH, frame.cols, frame.rows) };
        }
        else
        {
            int channels = static_cast<int>(outputShape[1]);
            int numGrid  = static_cast<int>(outputShape[2]);
//----------------------------------------------------
            //置信度阈值修改
//-------------------------------------------------
            return ParseRawYolo(outputData, channels, numGrid, 0.5f,
                                targetW, targetH, frame.cols, frame.rows,
                                letterScale, padX, padY);
        }
    }

    return {};
}

// 12. DrawResult() - 绘制检测结果

void VisionNode::DrawResult(cv::Mat & frame, const DetectionResult & det)
{
    if (det.type == BEAN_NONE) return;

    int cx = static_cast<int>((det.center_x + 1.0f) * frame.cols / 2.0f);
    int cy = static_cast<int>((det.center_y + 1.0f) * frame.rows / 2.0f);
    int sz = static_cast<int>(det.size * frame.cols);

    cv::Scalar color;
    switch (det.type)
    {
        case BEAN_SOY:    color = cv::Scalar(0, 255, 255); break;
        case BEAN_MUNG:   color = cv::Scalar(0, 200, 0);   break;
        case BEAN_KIDNEY: color = cv::Scalar(200, 200, 200); break;
        case DATA_1:      color = cv::Scalar(255, 100, 0); break;
        case DATA_2:      color = cv::Scalar(255, 0, 200); break;
        case DATA_3:      color = cv::Scalar(100, 200, 255); break;
        case DATA_4:      color = cv::Scalar(0, 100, 255); break;
        case DATA_5:      color = cv::Scalar(255, 0, 100); break;
        default:          color = cv::Scalar(0, 0, 255); break;
    }

    cv::rectangle(frame, cv::Rect(cx - sz / 2, cy - sz / 2, sz, sz), color, 2);

    std::string label = std::string(beanTypeName(
        static_cast<BeanType>(det.type)))
        + " " + std::to_string(static_cast<int>(det.confidence)) + "%";
    cv::putText(frame, label,
                cv::Point(cx - sz / 2, cy - sz / 2 - 10),
                cv::FONT_HERSHEY_SIMPLEX, 0.6, color, 2);

    uint8_t bin = beanTypeToBin(static_cast<BeanType>(det.type));
    std::string binText = "Bin " + std::to_string(bin);
    cv::putText(frame, binText,
                cv::Point(cx - sz / 2, cy + sz / 2 + 20),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, color, 1);
}

// 13. InferenceLoop() - 推理线程

void VisionNode::InferenceLoop()
{
    while (systemRunning_)
    {
        cv::Mat localFrame;
        {
            std::lock_guard<std::mutex> lock(frameMtx_);
            if (!sharedFrame_.empty())
                localFrame = sharedFrame_.clone();
        }
        if (!localFrame.empty())
        {
            auto dets = DetectBeans(localFrame);
            {
                std::lock_guard<std::mutex> lock(detsMtx_);
                lastDets_ = std::move(dets);
            }
        }
        //std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

// 14. SendDetection() - 发送检测结果

void VisionNode::SendDetection(const DetectionResult & det)
{
    DetectionPacket pkt;
    pkt.bean_type  = det.type;
    pkt.target_bin = beanTypeToBin(det.type);
    pkt.confidence = det.confidence;
    pkt.position_x = det.center_x;
    pkt.position_y = det.center_y;
    pkt.size       = det.size;

    auto tx = encodeDetection(pkt);
    if (serial_->SendFrame(tx.data(), tx.size()))
    {
        sendCount_++;
        std::cout << "[视觉] >> "
                  << beanTypeName(det.type)
                  << " -> " << static_cast<int>(pkt.target_bin) << "号箱"
                  << " (置信度=" << static_cast<int>(det.confidence) << "%)"
                  << std::endl;
    }
}

// 15. Run() - 主循环

int VisionNode::Run()
{
    const char * WIN_NAME = "抓豆分拣 - 视觉识别";
    cv::Mat frame;

    systemRunning_ = true;
    infThread_ = std::thread(&VisionNode::InferenceLoop, this);

    while (true)
    {
        // 1. 取帧
        if (config_.simulate)
        {
            frame = cv::Mat(480, 640, CV_8UC3, cv::Scalar(60, 60, 60));
            cv::putText(frame, "SIMULATED MODE",
                        cv::Point(150, 240), cv::FONT_HERSHEY_SIMPLEX, 0.8,
                        cv::Scalar(255, 255, 255), 2);
        }
        else
        {
            frame = camera_.GrabFrame();
            if (frame.empty())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(frameMtx_);
                frame.copyTo(sharedFrame_);
            }
        }

        // 2. 读取最新推理结果
        std::vector<DetectionResult> displayDets;
        {
            std::lock_guard<std::mutex> lock(detsMtx_);
            displayDets = lastDets_;
            if (config_.simulate)
            {
                displayDets.clear();
                static int counter = 0;
                if (++counter % 30 == 0)
                {
                    DetectionResult d;
                    d.type       = static_cast<BeanType>((rand() % 3) + 1);
                    d.confidence = 85;
                    d.center_x   = (rand() % 1000 - 500) / 500.0f;
                    d.center_y   = (rand() % 1000 - 500) / 500.0f;
                    d.size       = 0.15f;
                    displayDets.push_back(d);
                }
            }
        }

        // 3. 绘制所有检测结果
        for (auto & det : displayDets)
            DrawResult(frame, det);

        // 4. 信息叠加
        cv::putText(frame,
                    "Q退出 | 目标:" + std::to_string(displayDets.size()),
                    cv::Point(10, frame.rows - 15),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5,
                    cv::Scalar(200, 200, 200), 1);

        // 5. 显示
        if (config_.showWindow)
        {
            cv::imshow(WIN_NAME, frame);
            int key = cv::waitKey(1) & 0xFF;
            if (key == 'q' || key == 27) break;
        }

        // 6. 串口：收到就绪信号 → 发第一个检测结果
        auto status_frame = serial_->ReadFrame(
            STATUS_HEADER, STATUS_PACKET_SIZE, 1);
        if (!status_frame.empty())
        {
            StatusPacket status;
            if (decodeStatus(status_frame.data(), status))
            {
                if (status.flags & FLAG_READY)
                {
                    const DetectionResult * toSend = nullptr;
                    for (auto & d : displayDets)
                    {
                        int rawClass = static_cast<int>(d.type) - 1;
                        if (rawClass >= 0 && rawClass <= 7)
                        {
                            toSend = &d;
                            break;
                        }
                    }

                    if (toSend) SendDetection(*toSend);
                }
            }
            totalCount_++;
        }

        // 模拟模式：自动发送
        if (config_.simulate)
        {
            static auto last_sim_send = std::chrono::steady_clock::now();
            auto simNow = std::chrono::steady_clock::now();
            auto simElapsed = std::chrono::duration_cast<
                std::chrono::milliseconds>(simNow - last_sim_send).count();
            if (simElapsed > 2000)
            {
                last_sim_send = simNow;
                DetectionResult d;
                d.type       = static_cast<BeanType>((rand() % 3) + 1);
                d.confidence = 85;
                d.center_x   = (rand() % 1000 - 500) / 500.0f;
                d.center_y   = (rand() % 1000 - 500) / 500.0f;
                d.size       = 0.15f;

                SendDetection(d);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    Stop();
    return 0;
}

// 16. Stop() - 停止

void VisionNode::Stop()
{
    systemRunning_ = false;
    if (infThread_.joinable()) infThread_.join();

    cv::destroyAllWindows();
    serial_->Close();
    std::cout << "[视觉] 已退出" << std::endl;
}

}  // namespace bean_sorter

// 17. main() - 程序入口

int main(int argc, char * argv[])
{
    bean_sorter::VisionNode node;
    if (!node.Init(argc, argv)) return 1;
    return node.Run();
}