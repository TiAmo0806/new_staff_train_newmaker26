/**
 * @file VisionNode.cpp
 * @brief 视觉节点 - 迈德威视相机实时画面 + ONNX 豆子识别 + 串口通信
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
#include <atomic>
#include <memory>
#include <cmath>
#include <fstream>
#include <sstream>
#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>

namespace bean_sorter
{
// 稳定化阈值（必须定义在 Config 之前，因为 Config 用它们做默认值）
static const int STABLE_THRESHOLD = 5;       // 豆子连续几帧一致才发送
static const int DIGIT_STABLE_THRESHOLD = 3; // 数字连续几帧一致才发送

// 1. 配置结构体
struct Config
{
    std::string serialPort ="/dev/ttyACM0";
    std::string modelPath  = "model/best6.onnx";
    bool        simulate   = false;
    bool        txLog      = true;
    bool        rxLog      = false;
    int         cameraIndex = 0;
    bool        showWindow = true;

    // 从 config.yaml 读取的参数，优先级高于命令行硬编码
    double      confidence_threshold = 0.5;
    double      nms_iou_threshold    = 0.5;
    int         stable_threshold_beans = STABLE_THRESHOLD;
    int         digit_stable_threshold = DIGIT_STABLE_THRESHOLD;

    void LoadYaml(const std::string & path)
    {
        std::ifstream f(path);
        if (!f.is_open()) return;
        std::string line, section;
        while (std::getline(f, line))
        {
            auto trim = [](std::string & s) {
                s.erase(0, s.find_first_not_of(" \t\r"));
                s.erase(s.find_last_not_of(" \t\r") + 1);
            };
            trim(line);
            if (line.empty() || line[0] == '#') continue;
            if (line.back() == ':') { section = line; continue; }
            auto pos = line.find(':');
            if (pos == std::string::npos) continue;
            std::string key = line.substr(0, pos);
            std::string val = line.substr(pos + 1);
            trim(key); trim(val);
            // 去掉行内注释
            auto cmt = val.find('#');
            if (cmt != std::string::npos) val = val.substr(0, cmt);
            trim(val);
            // 去掉引号
            if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
                val = val.substr(1, val.size() - 2);
            if      (key == "confidence_threshold")    confidence_threshold = std::stod(val);
            else if (key == "nms_iou_threshold")       nms_iou_threshold    = std::stod(val);
            else if (key == "stable_threshold_beans")  stable_threshold_beans = std::stoi(val);
            else if (key == "digit_stable_threshold")  digit_stable_threshold = std::stoi(val);
            else if (key == "simulated_mode" && section == "debug:") simulate = (val == "true");
            else if (key == "tx_log")   txLog   = (val == "true");
            else if (key == "rx_log")   rxLog   = (val == "true");
            else if (key == "vision_port") serialPort = val;
        }
        std::cout << "[视觉] 加载配置: confidence=" << confidence_threshold
                  << " 豆子稳定=" << stable_threshold_beans
                  << " 数字稳定=" << digit_stable_threshold << std::endl;
    }
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

// 位置容差（center_x 差异小于此值认为位置相同）
static const float POS_TOLERANCE = 0.03f;

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

    static DetectionResult ParseOutput(//用来把模型的数学结果变成坐标框的
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

    void SendCombinedDetections(const std::vector<DetectionResult> & dets);
    void SendDigitDetections(const std::vector<DetectionResult> & dets);
    bool DetsMatch(const std::vector<DetectionResult> & a,
                   const std::vector<DetectionResult> & b);

    Config                      config_;
    std::unique_ptr<SerialPort> serial_;
    //推理模型有关成员变量
    Ort::Env                    onnxEnv_;//使用onnxruntime
    Ort::SessionOptions         sessionOpts_;
    Ort::Session                session_;
    std::array<int64_t, 4>      inputShape_;//用于存储 ONNX 模型的输入张量形状

    CameraDriver                camera_;
    //稳定化有关
    std::vector<DetectionResult> prevBeanDets_;//上一帧豆子检测结果
    int                         beanStableCount_ = 0;//豆子连续稳定帧数
    bool                        beanSent_ = false;//本轮周期豆子是否已发送

    std::vector<DetectionResult> prevDigitDets_;//上一帧数字检测结果
    int                         digitStableCount_ = 0;//数字连续稳定帧数
    bool                        digitSent_ = false;//本轮周期数字是否已发送

    std::atomic<bool>           systemRunning_;//控制线程运行或停止
    std::vector<uint8_t>        cmdBuffer_;//串口命令接收缓冲区

    int sendCount_;
};

// 4. 构造函数 / 析构函数
VisionNode::VisionNode()
    : serial_(nullptr)
    , onnxEnv_(nullptr)
    , session_(nullptr)
    , systemRunning_(false)
    , sendCount_(0)
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

    // 从 config.yaml 加载额外参数（覆盖默认值）
    config_.LoadYaml("config.yaml");

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
    serial_->SetAutoReconnect(false);

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
    auto kept = Nms(results, config_.nms_iou_threshold, frameW, frameH);

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
                        static_cast<int>(outputShape[2]), 6, config_.confidence_threshold,
                        targetW, targetH, frame.cols, frame.rows) };
        }
        else if (outputShape[2] == 6)
        {
            return { ParseOutput(outputData,
                        static_cast<int>(outputShape[1]), 6, config_.confidence_threshold,
                        targetW, targetH, frame.cols, frame.rows) };
        }
        else
        {
            int channels = static_cast<int>(outputShape[1]);
            int numGrid  = static_cast<int>(outputShape[2]);
//----------------------------------------------------
            //置信度阈值修改
//-------------------------------------------------
            return ParseRawYolo(outputData, channels, numGrid, config_.confidence_threshold,
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

// 14. SendCombinedDetections() - 打包结果为1个9字节帧发送

void VisionNode::SendCombinedDetections(const std::vector<DetectionResult> & dets)
{
    if (dets.empty()) return;

    // 只保留豆子类（排除数字标签）
    std::vector<DetectionResult> beans;
    for (auto & d : dets)
    {
        if (d.type >= BEAN_SOY && d.type <= BEAN_KIDNEY)
            beans.push_back(d);
    }
    if (beans.empty()) return;

    // 按 center_x 升序排列（从左到右）
    std::sort(beans.begin(), beans.end(),
        [](const DetectionResult & a, const DetectionResult & b) {
            return a.center_x < b.center_x;
        });

    DetectionPacket pkt{};

    // 填充3个固定位置（左/中/右），不足的填0
    for (int i = 0; i < 3; ++i)
    {
        if (i < static_cast<int>(beans.size()))
        {
            pkt.bean_types[i]  = static_cast<uint8_t>(beans[i].type);
            pkt.target_bins[i] = beanTypeToBin(beans[i].type);
        }
        else
        {
            pkt.bean_types[i]  = BEAN_NONE;
            pkt.target_bins[i] = 0;
        }
    }

    auto tx = encodeDetection(pkt);
    // 连续发3次，防止串口丢包导致电控收不到
    for (int r = 0; r < 3; ++r)
    {
        serial_->SendFrame(tx.data(), tx.size());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    sendCount_++;
    std::cout << "[视觉] >> 3位置包(x3): ";
    for (int i = 0; i < 3; ++i)
    {
        if (pkt.bean_types[i] != BEAN_NONE)
            std::cout << "pos" << (i+1) << "="
                      << beanTypeName(static_cast<BeanType>(pkt.bean_types[i]))
                      << "->" << static_cast<int>(pkt.target_bins[i]) << "号箱 ";
        else
            std::cout << "pos" << (i+1) << "=空 ";
    }
    std::cout << std::endl;
}

// 14b. SendDigitDetections() - 发送数字标签检测结果（0xCC包）

void VisionNode::SendDigitDetections(const std::vector<DetectionResult> & dets)
{
    if (dets.empty()) return;

    // 筛选出数字标签类 (DATA_1 ~ DATA_5)
    std::vector<DetectionResult> digits;
    for (auto & d : dets)
    {
        if (d.type >= DATA_1 && d.type <= DATA_5)
            digits.push_back(d);
    }
    if (digits.empty()) return;

    // 按 center_x 排序（左到右）
    std::sort(digits.begin(), digits.end(),
        [](const DetectionResult & a, const DetectionResult & b) {
            return a.center_x < b.center_x;
        });

    // 如果只检测到4个数字标签，推断缺失的那个（数字标签1-5固定排列）
    if (digits.size() == 4)
    {
        bool present[5] = {false};
        for (auto & d : digits)
        {
            int num = static_cast<int>(d.type) - 3;  // DATA_1=4 → 1
            if (num >= 1 && num <= 5) present[num - 1] = true;
        }
        uint8_t missingNum = 0;
        for (int i = 0; i < 5; ++i)
        {
            if (!present[i]) { missingNum = i + 1; break; }
        }
        // 将缺失的数字追加到最后一个位置（不按顺序插入）
        DetectionResult synth;
        synth.type = static_cast<BeanType>(missingNum + 3);  // 1→DATA_1=4
        digits.push_back(synth);
        std::cout << "[视觉] 推断缺失数字: " << static_cast<int>(missingNum)
                  << " -> 位置" << (digits.size()) << std::endl;
    }

    NumberPacket pkt{};
    for (int i = 0; i < 5 && i < static_cast<int>(digits.size()); ++i)
    {
        // DATA_1=4 → 数字1, DATA_2=5 → 数字2, ...
        pkt.digits[i] = static_cast<uint8_t>(digits[i].type) - 3;
    }

    auto tx = encodeNumber(pkt);
    // 连续发3次，防止串口丢包导致电控收不到
    for (int r = 0; r < 3; ++r)
    {
        serial_->SendFrame(tx.data(), tx.size());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::cout << "[视觉] >> 数字标签包(x3): ";
    for (int i = 0; i < 5; ++i)
    {
        if (pkt.digits[i] != 0)
            std::cout << "pos" << (i+1) << "=" << static_cast<int>(pkt.digits[i]) << " ";
        else
            std::cout << "pos" << (i+1) << "=空 ";
    }
    std::cout << std::endl;
}

bool VisionNode::DetsMatch(const std::vector<DetectionResult> & a,
                           const std::vector<DetectionResult> & b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].type != b[i].type) return false;
        if (std::abs(a[i].center_x - b[i].center_x) > POS_TOLERANCE) return false;
        if (std::abs(a[i].center_y - b[i].center_y) > POS_TOLERANCE) return false;
    }
    return true;
}

// 15. Run() - 主循环

int VisionNode::Run()
{
    const char * WIN_NAME = "抓豆分拣 - 视觉识别";
    cv::Mat frame;

    systemRunning_ = true;

    while (true)
    {
        std::vector<DetectionResult> displayDets;

        // 1. 取帧 + 立即推理（合并）
        if (config_.simulate)
        {
            frame = cv::Mat(480, 640, CV_8UC3, cv::Scalar(60, 60, 60));
            cv::putText(frame, "SIMULATED MODE",
                        cv::Point(150, 240), cv::FONT_HERSHEY_SIMPLEX, 0.8,
                        cv::Scalar(255, 255, 255), 2);
            
            // 模拟模式：生成假数据
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
        else
        {
            // 取帧
            frame = camera_.GrabFrame();
            if (frame.empty())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            
            // 拿到帧后立即推理（单线程，框和画面同步）
            displayDets = DetectBeans(frame);
        }

        // 2. 绘制所有检测结果
        for (auto & det : displayDets)
            DrawResult(frame, det);

        // 3. 稳定化检测 - 豆子和数字各自独立稳定
        if (!config_.simulate)
        {
            // 分离豆子和数字检测结果
            std::vector<DetectionResult> beanDets, digitDets;
            for (auto & d : displayDets)
            {
                if (d.type >= BEAN_SOY && d.type <= BEAN_KIDNEY)
                    beanDets.push_back(d);
                if (d.type >= DATA_1 && d.type <= DATA_5)
                    digitDets.push_back(d);
            }

            // 按 center_x 排序（左到右）
            std::sort(beanDets.begin(), beanDets.end(),
                [](const DetectionResult & a, const DetectionResult & b) {
                    return a.center_x < b.center_x;
                });
            std::sort(digitDets.begin(), digitDets.end(),
                [](const DetectionResult & a, const DetectionResult & b) {
                    return a.center_x < b.center_x;
                });

            // — 豆子稳定化（3个都识别到才启动） —
            if (beanDets.size() >= 3)
            {
                if (DetsMatch(beanDets, prevBeanDets_))
                    beanStableCount_++;
                else
                {
                    prevBeanDets_ = beanDets;
                    beanStableCount_ = 0;
                    beanSent_ = false;
                }
            }
            else
            {
                prevBeanDets_.clear();
                beanStableCount_ = 0;
            }

            // — 数字稳定化（至少4个数字才启动，避免过渡阶段发不完整包） —
            if (digitDets.size() >= 4)
            {
                if (DetsMatch(digitDets, prevDigitDets_))
                    digitStableCount_++;
                else
                {
                    prevDigitDets_ = digitDets;
                    digitStableCount_ = 0;
                    digitSent_ = false;
                }
            }
            else
            {
                prevDigitDets_.clear();
                digitStableCount_ = 0;
            }

            // 豆子稳定 → 发 0xAA
            if (beanStableCount_ >= config_.stable_threshold_beans && !beanSent_)
            {
                SendCombinedDetections(beanDets);
                beanSent_ = true;
                std::cout << "[视觉] 豆子检测已发送，等待转到数字区..." << std::endl;
            }

            // 数字稳定 → 发 0xCC
            if (digitStableCount_ >= config_.digit_stable_threshold && !digitSent_)
            {
                SendDigitDetections(digitDets);
                digitSent_ = true;
            }

            // 两者都已发送 → 重置周期
            if (beanSent_ && digitSent_)
            {
                beanSent_ = false;
                digitSent_ = false;
                beanStableCount_ = 0;
                digitStableCount_ = 0;
                prevBeanDets_.clear();
                prevDigitDets_.clear();
                std::cout << "[视觉] 完整周期结束，重置" << std::endl;
            }
        }

        // 3b. 串口命令处理（仅保留读取缓冲，移除模式切换）
        // 现在豆子和数字由画面内容自动识别，不再依赖 FLAG_READY/FLAG_DIGIT_SCAN 切换
        {
            uint8_t tmp[256];
            size_t n = serial_->Read(tmp, sizeof(tmp));
            if (n > 0)
            {
                cmdBuffer_.insert(cmdBuffer_.end(), tmp, tmp + n);
                if (cmdBuffer_.size() > 4096)
                    cmdBuffer_.erase(cmdBuffer_.begin(), cmdBuffer_.begin() + cmdBuffer_.size() - 2048);
            }

            std::vector<uint8_t> cmd_frame;
            if (scanFrame(cmdBuffer_, STATUS_HEADER, STATUS_PACKET_SIZE, cmd_frame))
            {
                auto it = std::search(cmdBuffer_.begin(), cmdBuffer_.end(),
                                      cmd_frame.begin(), cmd_frame.end());
                if (it != cmdBuffer_.end())
                    cmdBuffer_.erase(cmdBuffer_.begin(), it + cmd_frame.size());
                else
                    cmdBuffer_.clear();

                StatusPacket status;
                if (decodeStatus(cmd_frame.data(), status))
                {
                    // 仅打印状态信息，不再切换模式
                    std::cout << "[视觉] 收到状态: state="
                              << static_cast<int>(status.system_state)
                              << " flags=" << static_cast<int>(status.flags)
                              << std::endl;
                }
            }
        }

        // 4. 信息叠加（显示稳定状态）
        {
            std::string stableInfo;
            if (config_.simulate)
                stableInfo = "SIM";
            else if (beanSent_ && digitSent_)
                stableInfo = "周期完成";
            else
                stableInfo = "豆子:" + std::to_string(beanStableCount_) + "/" + std::to_string(config_.stable_threshold_beans)
                           + " 数字:" + std::to_string(digitStableCount_) + "/" + std::to_string(config_.digit_stable_threshold);
            cv::putText(frame, stableInfo,
                        cv::Point(10, 25),
                        cv::FONT_HERSHEY_SIMPLEX, 0.5,
                        cv::Scalar(200, 200, 200), 1);
        }

        // 5. 显示
        if (config_.showWindow)
        {
            cv::imshow(WIN_NAME, frame);
            int key = cv::waitKey(1) & 0xFF;
            if (key == 'q' || key == 27) break;
        }

        // 6. 模拟模式：自动发送（仅测试用）
        if (config_.simulate)
        {
            static auto last_sim_send = std::chrono::steady_clock::now();
            auto simNow = std::chrono::steady_clock::now();
            auto simElapsed = std::chrono::duration_cast<
                std::chrono::milliseconds>(simNow - last_sim_send).count();
            if (simElapsed > 2000)
            {
                last_sim_send = simNow;
                std::vector<DetectionResult> simDets;

                // 模拟3个从左到右的豆子
                float positions[] = {-0.6f, 0.0f, 0.6f};
                for (int i = 0; i < 3; ++i)
                {
                    DetectionResult d;
                    d.type       = static_cast<BeanType>((rand() % 3) + 1);
                    d.confidence = 85;
                    d.center_x   = positions[i];
                    d.center_y   = (rand() % 400 - 200) / 500.0f;
                    d.size       = 0.15f;
                    simDets.push_back(d);
                }

                SendCombinedDetections(simDets);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }  // ← while(true) 结束

    Stop();
    return 0;
}  // ← Run() 结束


// 16. Stop() - 停止

void VisionNode::Stop()
{
    systemRunning_ = false;

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