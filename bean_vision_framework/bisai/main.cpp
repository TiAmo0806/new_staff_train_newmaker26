/**
 * 视频目标检测 —— 豆子与数字识别
 *
 * 使用 YOLOv8 ONNX 模型 + ONNX Runtime 进行推理，
 * 在图像上框出检测目标并标注类别与置信度。
 *
 * 用法:
 *   bean_number_detector [模型路径] [视频路径]
 */

#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

constexpr int INPUT_WIDTH = 640;
constexpr int INPUT_HEIGHT = 640;
constexpr int FONT = cv::FONT_HERSHEY_SIMPLEX;
constexpr double FONT_SCALE = 0.6;
constexpr int FONT_THICKNESS = 2;
constexpr int LINE_THICKNESS = 2;

const std::vector<std::string> CLASS_NAMES = {
    "soybean",
    "mung_bean",
    "white_kidney_bean",
    "data_1",
    "data_2",
    "data_3",
    "data_4",
    "data_5"
};

struct Detection {
    int classId;
    float confidence;
    cv::Rect2f box;
};

std::vector<cv::Scalar> buildColorTable(int numClasses) {
    std::vector<cv::Scalar> colors(numClasses);
    colors[0] = cv::Scalar(0, 255, 0);
    for (int i = 1; i < numClasses && i <= 10; ++i) {
        int hue = (i - 1) * 25;
        cv::Mat hsv(1, 1, CV_8UC3, cv::Scalar(hue, 220, 220));
        cv::Mat bgr;
        cv::cvtColor(hsv, bgr, cv::COLOR_HSV2BGR);
        colors[i] = cv::Scalar(
            bgr.at<cv::Vec3b>(0, 0)[0],
            bgr.at<cv::Vec3b>(0, 0)[1],
            bgr.at<cv::Vec3b>(0, 0)[2]);
    }
    return colors;
}

cv::Mat preprocess(const cv::Mat& src, int& padX, int& padY, float& scale) {
    int w = src.cols;
    int h = src.rows;
    scale = std::min(static_cast<float>(INPUT_WIDTH) / w,
                     static_cast<float>(INPUT_HEIGHT) / h);
    int newW = static_cast<int>(w * scale);
    int newH = static_cast<int>(h * scale);
    padX = (INPUT_WIDTH - newW) / 2;
    padY = (INPUT_HEIGHT - newH) / 2;
    int right = INPUT_WIDTH - newW - padX;
    int bottom = INPUT_HEIGHT - newH - padY;

    cv::Mat resized;
    cv::resize(src, resized, cv::Size(newW, newH));
    cv::Mat padded(INPUT_HEIGHT, INPUT_WIDTH, CV_8UC3, cv::Scalar(114, 114, 114));
    resized.copyTo(padded(cv::Rect(padX, padY, INPUT_WIDTH - padX - right, INPUT_HEIGHT - padY - bottom)));

    return cv::dnn::blobFromImage(
        padded,
        1.0 / 255.0,
        cv::Size(INPUT_WIDTH, INPUT_HEIGHT),
        cv::Scalar(),
        true,
        false);
}

std::vector<Detection> parseYOLOv8Output(
    const float* data,
    int numClasses,
    int numAnchors,
    int imgW,
    int imgH,
    float scale,
    int padX,
    int padY,
    float confThreshold,
    float nmsThreshold) {
    std::vector<cv::Rect> boxes;
    std::vector<float> scores;
    std::vector<int> classIds;

    for (int a = 0; a < numAnchors; ++a) {
        int bestClass = -1;
        float bestScore = -1.0f;

        for (int c = 0; c < numClasses; ++c) {
            float score = data[(4 + c) * numAnchors + a];
            if (score > bestScore) {
                bestScore = score;
                bestClass = c;
            }
        }

        if (bestScore < confThreshold) {
            continue;
        }

        float cx = data[a];
        float cy = data[numAnchors + a];
        float w = data[2 * numAnchors + a];
        float h = data[3 * numAnchors + a];

        float x1 = (cx - w * 0.5f - padX) / scale;
        float y1 = (cy - h * 0.5f - padY) / scale;
        float x2 = (cx + w * 0.5f - padX) / scale;
        float y2 = (cy + h * 0.5f - padY) / scale;

        x1 = std::max(0.0f, std::min(x1, static_cast<float>(imgW - 1)));
        y1 = std::max(0.0f, std::min(y1, static_cast<float>(imgH - 1)));
        x2 = std::max(0.0f, std::min(x2, static_cast<float>(imgW - 1)));
        y2 = std::max(0.0f, std::min(y2, static_cast<float>(imgH - 1)));

        int boxX = static_cast<int>(std::round(x1));
        int boxY = static_cast<int>(std::round(y1));
        int boxW = static_cast<int>(std::round(x2 - x1));
        int boxH = static_cast<int>(std::round(y2 - y1));
        if (boxW <= 0 || boxH <= 0) {
            continue;
        }

        boxes.emplace_back(boxX, boxY, boxW, boxH);
        scores.push_back(bestScore);
        classIds.push_back(bestClass);
    }

    std::vector<int> indices;
    cv::dnn::NMSBoxes(boxes, scores, confThreshold, nmsThreshold, indices);

    std::vector<Detection> detections;
    detections.reserve(indices.size());
    for (int index : indices) {
        detections.push_back({
            classIds[index],
            scores[index],
            cv::Rect2f(
                static_cast<float>(boxes[index].x),
                static_cast<float>(boxes[index].y),
                static_cast<float>(boxes[index].width),
                static_cast<float>(boxes[index].height))
        });
    }

    std::sort(detections.begin(), detections.end(), [](const Detection& a, const Detection& b) {
        return a.confidence > b.confidence;
    });
    return detections;
}

void drawDetections(
    cv::Mat& frame,
    const std::vector<Detection>& detections,
    const std::vector<cv::Scalar>& colors) {
    for (const auto& det : detections) {
        const auto& color = colors[det.classId % colors.size()];
        const auto& name = (det.classId < static_cast<int>(CLASS_NAMES.size()))
            ? CLASS_NAMES[det.classId]
            : CLASS_NAMES[0];

        cv::Rect rect(
            static_cast<int>(det.box.x),
            static_cast<int>(det.box.y),
            static_cast<int>(det.box.width),
            static_cast<int>(det.box.height));

        cv::rectangle(frame, rect, color, LINE_THICKNESS);

        char label[128];
        std::snprintf(label, sizeof(label), "%s %.2f", name.c_str(), det.confidence);
        int baseline = 0;
        cv::Size textSize = cv::getTextSize(
            label,
            FONT,
            FONT_SCALE,
            FONT_THICKNESS,
            &baseline);

        cv::Point topLeft(rect.x, rect.y - textSize.height - 6);
        cv::Point bottomRight(rect.x + textSize.width + 4, rect.y);
        if (topLeft.y < 0) {
            topLeft.y = rect.y;
            bottomRight.y = rect.y + textSize.height + 4;
        }
        cv::rectangle(frame, topLeft, bottomRight, color, cv::FILLED);
        cv::putText(
            frame,
            label,
            cv::Point(rect.x + 2, bottomRight.y - baseline - 2),
            FONT,
            FONT_SCALE,
            cv::Scalar(255, 255, 255),
            FONT_THICKNESS,
            cv::LINE_AA);
    }
}

void drawStats(cv::Mat& frame, int beanCount, int numberCount, double fps, double latencyMs) {
    char buffer[256];
    int y = 28;
    auto put = [&](const std::string& text, cv::Scalar color) {
        cv::putText(frame, text, cv::Point(10, y), FONT, 0.7, color, 2, cv::LINE_AA);
        y += 28;
    };

    std::snprintf(buffer, sizeof(buffer), "Beans : %d", beanCount);
    put(buffer, cv::Scalar(0, 255, 0));
    std::snprintf(buffer, sizeof(buffer), "Numbers: %d", numberCount);
    put(buffer, cv::Scalar(255, 0, 0));
    std::snprintf(buffer, sizeof(buffer), "FPS   : %.1f", fps);
    put(buffer, cv::Scalar(255, 255, 255));
    std::snprintf(buffer, sizeof(buffer), "Latency: %.1f ms", latencyMs);
    put(buffer, cv::Scalar(200, 200, 200));
}

void applyFlip(cv::Mat& frame, int flipMode) {
    if (flipMode == 1) {
        cv::flip(frame, frame, 1);
    } else if (flipMode == 2) {
        cv::flip(frame, frame, 0);
    } else if (flipMode == 3) {
        cv::flip(frame, frame, -1);
    }
}

bool hasImageExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp";
}

std::vector<std::string> collectImagePaths(const std::string& inputPath) {
    namespace fs = std::filesystem;
    std::vector<std::string> imagePaths;
    fs::path path(inputPath);

    if (!fs::exists(path)) {
        return imagePaths;
    }

    if (fs::is_directory(path)) {
        for (const auto& entry : fs::directory_iterator(path)) {
            if (entry.is_regular_file() && hasImageExtension(entry.path())) {
                imagePaths.push_back(entry.path().string());
            }
        }
        std::sort(imagePaths.begin(), imagePaths.end());
        return imagePaths;
    }

    if (hasImageExtension(path)) {
        imagePaths.push_back(path.string());
        return imagePaths;
    }

    if (path.extension() == ".txt" || path.extension() == ".lst") {
        std::ifstream input(path);
        std::string line;
        while (std::getline(input, line)) {
            if (!line.empty()) {
                imagePaths.push_back(line);
            }
        }
        return imagePaths;
    }

    return imagePaths;
}

void printDetections(
    const std::string& sourceName,
    const std::vector<Detection>& detections) {
    for (const auto& det : detections) {
        const std::string& name = (det.classId < static_cast<int>(CLASS_NAMES.size()))
            ? CLASS_NAMES[det.classId]
            : CLASS_NAMES[0];
        std::cout << "source=" << sourceName
                  << " class=" << name
                  << " confidence=" << det.confidence
                  << " box=["
                  << det.box.x << ","
                  << det.box.y << ","
                  << det.box.width << ","
                  << det.box.height << "]"
                  << std::endl;
    }
}

std::string buildOutputImagePath(
    const std::string& outputDir,
    const std::string& sourcePath,
    size_t index) {
    namespace fs = std::filesystem;
    fs::path source(sourcePath);
    fs::path outDir(outputDir);
    std::string filename;
    if (source.has_filename()) {
        filename = source.filename().string();
    } else {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%06zu.jpg", index + 1);
        filename = buffer;
    }
    return (outDir / filename).string();
}

int main(int argc, char** argv) {
    std::string modelPath = "model/best.onnx";
    std::string inputPath = "frames";
    std::string outputDir = "output";
    float confThreshold = 0.25f;
    float nmsThreshold = 0.45f;
    int flipMode = 0;
    if (argc >= 2) {
        modelPath = argv[1];
    }
    if (argc >= 3) {
        inputPath = argv[2];
    }
    if (argc >= 4) {
        outputDir = argv[3];
    }
    if (argc >= 5) {
        confThreshold = std::stof(argv[4]);
    }
    if (argc >= 6) {
        nmsThreshold = std::stof(argv[5]);
    }
    if (argc >= 7) {
        flipMode = std::stoi(argv[6]);
    }

    std::cout << "[INFO] 正在加载模型: " << modelPath << std::endl;
    std::cout << "[INFO] 输入路径: " << inputPath << std::endl;
    std::cout << "[INFO] 输出目录: " << outputDir << std::endl;
    std::cout << "[INFO] conf_threshold=" << confThreshold
              << " nms_threshold=" << nmsThreshold
              << " flip_mode=" << flipMode << std::endl;

    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "BeanNumberDetector");
    Ort::SessionOptions sessionOptions;
    sessionOptions.SetIntraOpNumThreads(4);
    sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    Ort::Session session(env, modelPath.c_str(), sessionOptions);

    Ort::AllocatorWithDefaultOptions allocator;
    std::vector<const char*> inputNames;
    std::vector<Ort::AllocatedStringPtr> inputNamePtrs;
    size_t numInputNodes = session.GetInputCount();
    for (size_t i = 0; i < numInputNodes; ++i) {
        inputNamePtrs.push_back(session.GetInputNameAllocated(i, allocator));
        inputNames.push_back(inputNamePtrs.back().get());
    }

    std::vector<const char*> outputNames;
    std::vector<Ort::AllocatedStringPtr> outputNamePtrs;
    size_t numOutputNodes = session.GetOutputCount();
    for (size_t i = 0; i < numOutputNodes; ++i) {
        outputNamePtrs.push_back(session.GetOutputNameAllocated(i, allocator));
        outputNames.push_back(outputNamePtrs.back().get());
    }

    auto outputTypeInfo = session.GetOutputTypeInfo(0);
    auto outputTensorInfo = outputTypeInfo.GetTensorTypeAndShapeInfo();
    std::vector<int64_t> outputShape = outputTensorInfo.GetShape();
    if (outputShape.size() != 3 || outputShape[1] < 5 || outputShape[2] <= 0) {
        std::cerr << "[ERROR] 模型输出 shape 异常: ";
        for (size_t i = 0; i < outputShape.size(); ++i) {
            std::cerr << (i == 0 ? "[" : ", ") << outputShape[i];
        }
        std::cerr << "]" << std::endl;
        return 1;
    }

    int outputChannels = static_cast<int>(outputShape[1]);
    int numClasses = outputChannels - 4;
    int numAnchors = static_cast<int>(outputShape[2]);
    std::cout << "[INFO] 输出 shape: ["
              << outputShape[0] << ", "
              << outputShape[1] << ", "
              << outputShape[2] << "]" << std::endl;
    if (numClasses != static_cast<int>(CLASS_NAMES.size())) {
        std::cerr << "[WARN] 模型类别数=" << numClasses
                  << "，内置类别名数=" << CLASS_NAMES.size()
                  << "，请确认类别顺序一致" << std::endl;
    }
    Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::vector<std::string> imagePaths = collectImagePaths(inputPath);
    std::filesystem::create_directories(outputDir);

    auto colors = buildColorTable(numClasses);
    std::vector<double> latencyRing(30, 0.0);
    size_t ringIdx = 0;

    if (!imagePaths.empty()) {
        std::cout << "[INFO] 使用图片序列输入, 共 " << imagePaths.size() << " 张" << std::endl;
        for (size_t imageIndex = 0; imageIndex < imagePaths.size(); ++imageIndex) {
            const auto& imagePath = imagePaths[imageIndex];
            cv::Mat frame = cv::imread(imagePath);
            if (frame.empty()) {
                std::cerr << "[WARN] 无法读取图片: " << imagePath << std::endl;
                continue;
            }
            applyFlip(frame, flipMode);

            auto t0 = std::chrono::steady_clock::now();

            int padX = 0;
            int padY = 0;
            float scale = 1.0f;
            cv::Mat blob = preprocess(frame, padX, padY, scale);

            std::vector<int64_t> inputShape = {1, 3, INPUT_HEIGHT, INPUT_WIDTH};
            Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
                memoryInfo,
                blob.ptr<float>(),
                blob.total(),
                inputShape.data(),
                inputShape.size());

            auto outputs = session.Run(
                Ort::RunOptions{nullptr},
                inputNames.data(),
                &inputTensor,
                1,
                outputNames.data(),
                outputNames.size());

            std::vector<Detection> detections;
            if (!outputs.empty()) {
                const float* outputData = outputs[0].GetTensorData<float>();
                detections = parseYOLOv8Output(
                    outputData,
                    numClasses,
                    numAnchors,
                    frame.cols,
                    frame.rows,
                    scale,
                    padX,
                    padY,
                    confThreshold,
                    nmsThreshold);
            }

            printDetections(imagePath, detections);

            int beanCount = 0;
            int numberCount = 0;
            for (const auto& det : detections) {
                if (det.classId >= 0 && det.classId <= 2) {
                    ++beanCount;
                } else if (det.classId >= 3 && det.classId <= 7) {
                    ++numberCount;
                }
            }

            drawDetections(frame, detections, colors);

            auto t1 = std::chrono::steady_clock::now();
            double latencyMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
            latencyRing[ringIdx % latencyRing.size()] = latencyMs;
            ++ringIdx;
            double avgLatency = (ringIdx >= latencyRing.size())
                ? std::accumulate(latencyRing.begin(), latencyRing.end(), 0.0) / latencyRing.size()
                : latencyMs;
            double fps = avgLatency > 0.0 ? 1000.0 / avgLatency : 0.0;

            drawStats(frame, beanCount, numberCount, fps, avgLatency);
            std::string outputImagePath = buildOutputImagePath(outputDir, imagePath, imageIndex);
            cv::imwrite(outputImagePath, frame);
        }

        return 0;
    }

    cv::VideoCapture cap(inputPath);
    if (!cap.isOpened()) {
        std::cerr << "[ERROR] 无法打开视频源: " << inputPath << std::endl;
        std::cerr << "[ERROR] 当前构建建议改用图片序列目录、单张图片或图片列表文件作为输入" << std::endl;
        return 1;
    }

    cv::Mat frame;
    while (cap.read(frame)) {
        applyFlip(frame, flipMode);
        auto t0 = std::chrono::steady_clock::now();

        int padX = 0;
        int padY = 0;
        float scale = 1.0f;
        cv::Mat blob = preprocess(frame, padX, padY, scale);

        std::vector<int64_t> inputShape = {1, 3, INPUT_HEIGHT, INPUT_WIDTH};
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memoryInfo,
            blob.ptr<float>(),
            blob.total(),
            inputShape.data(),
            inputShape.size());

        auto outputs = session.Run(
            Ort::RunOptions{nullptr},
            inputNames.data(),
            &inputTensor,
            1,
            outputNames.data(),
            outputNames.size());

        std::vector<Detection> detections;
        if (!outputs.empty()) {
            const float* outputData = outputs[0].GetTensorData<float>();
            detections = parseYOLOv8Output(
                outputData,
                numClasses,
                numAnchors,
                frame.cols,
                frame.rows,
                scale,
                padX,
                padY,
                confThreshold,
                nmsThreshold);
        }

        printDetections("video_frame", detections);

        int beanCount = 0;
        int numberCount = 0;
        for (const auto& det : detections) {
            if (det.classId >= 0 && det.classId <= 2) {
                ++beanCount;
            } else if (det.classId >= 3 && det.classId <= 7) {
                ++numberCount;
            }
        }

        drawDetections(frame, detections, colors);

        auto t1 = std::chrono::steady_clock::now();
        double latencyMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        latencyRing[ringIdx % latencyRing.size()] = latencyMs;
        ++ringIdx;
        double avgLatency = (ringIdx >= latencyRing.size())
            ? std::accumulate(latencyRing.begin(), latencyRing.end(), 0.0) / latencyRing.size()
            : latencyMs;
        double fps = avgLatency > 0.0 ? 1000.0 / avgLatency : 0.0;

        drawStats(frame, beanCount, numberCount, fps, avgLatency);
    }

    cap.release();
    return 0;
}
