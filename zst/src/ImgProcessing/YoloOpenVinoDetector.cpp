#include "ImgProcessing/YoloOpenVinoDetector.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>

namespace
{
// 类别顺序必须与训练YOLO模型时的数据集类别顺序完全一致。
// 0～2是豆子，3～7是数字箱。
const std::vector<std::string> kClassNames = {
    "soybean",
    "mung_bean",
    "white_kidney_bean",
    "data_1",
    "data_2",
    "data_3",
    "data_4",
    "data_5"
};
// 计算两个矩形框的IoU。
// IoU = 交集面积 / 并集面积，范围是0～1。
float iou(const cv::Rect &a, const cv::Rect &b)
{
    const int intersectionArea = (a & b).area();

    const int unionArea =
        a.area() + b.area() - intersectionArea;

    if (unionArea <= 0)
    {
        return 0.0f;
    }

    return static_cast<float>(intersectionArea) /
           static_cast<float>(unionArea);
}
// 把张量形状转换为字符串。
// 例如：{1, 3, 640, 640}转换成"[1,3,640,640]"。
//
// 使用模板是因为：
// ov::Shape通常是vector<size_t>，
// postprocess中的shape是vector<int64_t>。
template <typename T>
std::string shapeToString(const std::vector<T> &shape)
{
    std::ostringstream oss;

    oss << '[';

    for (std::size_t i = 0; i < shape.size(); ++i)
    {
        if (i != 0)
        {
            oss << ',';
        }

        oss << shape[i];
    }

    oss << ']';

    return oss.str();
}
} // namespace
YoloOpenVinoDetector::YoloOpenVinoDetector(
    const YoloConfig &config)
    : config_(config)
{
    // 检查模型文件是否存在。
    if (!std::filesystem::is_regular_file(config_.modelPath))
    {
        throw std::runtime_error(
            "OpenVINO model file not found: " +
            config_.modelPath);
    }
    // 如果设置了OpenVINO缓存目录，就创建目录并启用编译缓存。
    //
    // 第一次运行时，OpenVINO会编译模型并保存缓存。
    // 后续运行时可以直接读取缓存，缩短启动时间。
    if (!config_.cacheDir.empty())
    {
        std::error_code ec;
        std::filesystem::create_directories(
            config_.cacheDir,
            ec);
        if (ec)
        {
            throw std::runtime_error(
                "cannot create OpenVINO cache directory: " +
                config_.cacheDir +
                ": " +
                ec.message());
        }
        core_.set_property(
            ov::cache_dir(config_.cacheDir));
    }
    // OpenVINO编译模型时使用的属性。
    ov::AnyMap compileProperties;
    // intraOpThreads大于0时，手动指定推理线程数。
    // 等于0时，让OpenVINO自动选择。
    if (config_.intraOpThreads > 0)
    {
        compileProperties.emplace(
            ov::inference_num_threads.name(),
            config_.intraOpThreads);
        }
    // 加载并编译ONNX模型。
    compiledModel_ = core_.compile_model(
        config_.modelPath,
        config_.device,
        compileProperties);
    // 创建一次InferRequest，之后每一帧重复使用。
    // 不需要每次推理时都重新加载模型。
    inferRequest_ =
        compiledModel_.create_infer_request();
    // 本工程要求模型必须只有一个输入和一个输出。
    if (compiledModel_.inputs().size() != 1 ||
        compiledModel_.outputs().size() != 1)
    {
        throw std::runtime_error(
            "best5.onnx must have exactly one input and one output");
    }
    // 获取模型输入端口和输出端口。
    const ov::Output<const ov::Node> inputPort =
        compiledModel_.input();
    const ov::Output<const ov::Node> outputPort =
        compiledModel_.output();
    // 获取模型真实输入、输出形状。
    const ov::Shape inputShape =
        inputPort.get_shape();
    const ov::Shape outputShape =
        outputPort.get_shape();
    // 程序期望的输入形状：
    // [1, 3, inputHeight, inputWidth]
    const ov::Shape expectedInputShape{
        1U,
        3U,
        static_cast<std::size_t>(config_.inputHeight),
        static_cast<std::size_t>(config_.inputWidth)
    };
    // 检查模型输入尺寸是否与配置一致。
    if (inputShape != expectedInputShape)
    {
        throw std::runtime_error(
            "best5.onnx input shape " +
            shapeToString(inputShape) +
            " does not match configured " +
            shapeToString(expectedInputShape));
    }
    // 输入和输出都必须是float32。
    if (inputPort.get_element_type() != ov::element::f32 ||
        outputPort.get_element_type() != ov::element::f32)
    {
        throw std::runtime_error(
            "best5.onnx input/output must be float32");
    }
    // 已经通过终端输出确认模型的输出固定为：[1, 12, 8400]
    const ov::Shape expectedOutputShape{
        1U,
        12U,
        8400U
    };
    // 输出必须严格等于[1,12,8400]。
    if (outputShape != expectedOutputShape)
    {
        throw std::runtime_error(
            "best5.onnx output shape does not match project: output=" +
            shapeToString(outputShape) +
            ", expected [1,12,8400]");
    }
    // 输出模型加载信息，方便调试。
    std::cout
        << "[OpenVINO] 模型加载成功: "
        << config_.modelPath
        << std::endl;
    std::cout
        << "[OpenVINO] 设备="
        << config_.device
        << "，输入="
        << shapeToString(inputShape)
        << "，输出="
        << shapeToString(outputShape)
        << std::endl;
    std::cout
        << "[OpenVINO] 编译缓存="
        << (config_.cacheDir.empty()
                ? std::string("关闭")
                : config_.cacheDir)
        << "，推理线程="
        << (config_.intraOpThreads > 0
                ? std::to_string(config_.intraOpThreads)
                : std::string("auto"))
        << std::endl;
}
cv::Mat YoloOpenVinoDetector::letterbox(
    const cv::Mat &image,
    LetterBoxInfo &info) const
{
    // 计算宽度方向和高度方向的缩放比例。
    const float widthScale =
        static_cast<float>(config_.inputWidth) /
        static_cast<float>(image.cols);

    const float heightScale =
        static_cast<float>(config_.inputHeight) /
        static_cast<float>(image.rows);
    // 使用较小的缩放比例，保证整张原图都能放进模型输入区域。
    const float scale =
        std::min(widthScale, heightScale);
    // 计算等比例缩放后的尺寸。
    const int newWidth =
        static_cast<int>(
            std::round(image.cols * scale));

    const int newHeight =
        static_cast<int>(
            std::round(image.rows * scale));
    // 保存缩放比例，后处理还原坐标时使用。
    info.scale = scale;
    // 剩余空间平均分配到左右两侧，所以除以2。
    info.padX =(config_.inputWidth - newWidth) / 2;
    // 剩余空间平均分配到上下两侧，所以除以2。
    info.padY =(config_.inputHeight - newHeight) / 2;
    // 等比例缩放原图。
    cv::Mat resized;
    cv::resize( image,resized,cv::Size(newWidth, newHeight));
    // 创建640×640灰色背景。
    // 114是Ultralytics常用的letterbox填充值。
    cv::Mat output(config_.inputHeight,config_.inputWidth,CV_8UC3,cv::Scalar(114, 114, 114));
    // 将缩放后的图片放在灰色背景中央。
    const cv::Rect targetArea( info.padX, info.padY,newWidth,newHeight);
    resized.copyTo(output(targetArea));
    return output;
}
std::vector<Detection> YoloOpenVinoDetector::infer(
    const cv::Mat &frame)
{
    // 输入图像为空时，直接返回空检测结果。
    if (frame.empty())
    {
        return {};
    }
    // 保存letterbox使用的缩放比例和灰边尺寸。
    LetterBoxInfo letterboxInfo;
    // 等比例缩放并补灰边。
    cv::Mat inputImage =
        letterbox(frame, letterboxInfo);
    // 用来保存模型输入数据。
    cv::Mat blob;
    // blobFromImage完成以下操作：
    // 1. 像素除以255，归一化到0～1
    // 2. BGR转换为RGB
    // 3. HWC转换为CHW
    // 4. uint8转换为float32
    // 5. 输出形状变成[1,3,640,640]
    cv::dnn::blobFromImage(inputImage, blob, 1.0 / 255.0,cv::Size(),cv::Scalar(), true, false, CV_32F);
    // 获取OpenVINO输入张量。
    ov::Tensor inputTensor =
        inferRequest_.get_input_tensor();
    // 检查输入张量的数据类型和元素数量。
    if (inputTensor.get_element_type() != ov::element::f32 ||
        inputTensor.get_size() != blob.total())
    {
        throw std::runtime_error(
            "OpenVINO input tensor no longer matches preprocessing output");
    }
    // 将OpenCV blob中的数据复制到OpenVINO输入张量。
    std::memcpy( inputTensor.data<float>(), blob.ptr<float>(),blob.total() * sizeof(float));
    // 执行同步推理。
    // 程序会等待这一帧推理完成后再继续。
    inferRequest_.infer();
    // 获取模型输出张量。
    ov::Tensor outputTensor =
        inferRequest_.get_output_tensor();
    // 获取输出形状，正常应该是[1,12,8400]。
    const ov::Shape outputShape =
        outputTensor.get_shape();
    // 转换成postprocess需要的vector<int64_t>。
    const std::vector<int64_t> shape(
        outputShape.begin(),
        outputShape.end());
    // 获取输出张量的数据指针。
    // const float表示只能读取数据，不能通过这个指针修改数据。
    const float *outputData =
        outputTensor.data<float>();
    // 解析YOLO输出并返回最终检测结果。
    return postprocess( outputData, shape,letterboxInfo,frame.size());
}
std::vector<Detection> YoloOpenVinoDetector::postprocess(const float *data,const std::vector<int64_t> &shape,const LetterBoxInfo &info, const cv::Size &imageSize) const
{
    // 本工程只接受固定输出[1,12,8400]。
    if (shape != std::vector<int64_t>{1, 12, 8400})
    {
        throw std::runtime_error(
            "unexpected YOLO output shape: " +
            shapeToString(shape) +
            ", expected [1,12,8400]");
    }
    // 模型一共输出8400个候选框。
    const int boxes = 8400;
    // 保存通过置信度筛选的矩形框。
    std::vector<cv::Rect> candidateBoxes;
    // 保存每个矩形框对应的最高类别分数。
    std::vector<float> candidateScores;
    // 保存每个矩形框对应的类别编号。
    std::vector<int> candidateClassIds;
    // 逐个处理8400个候选框。
    for (int i = 0; i < boxes; ++i)
    {
        // 输出形状为[1,12,8400]。
        // 内存中的数据排列为：
        // data[0～8399]       ：所有候选框的cx
        // data[8400～16799]   ：所有候选框的cy
        // data[16800～25199]  ：所有候选框的w
        // data[25200～33599]  ：所有候选框的h
        // data[33600～41999]  ：第0类的分数
        // 后面依次是第1～7类的分数
        const float cx =
            data[i];
        const float cy =
            data[boxes + i];
        const float boxWidth =
            data[2 * boxes + i];
        const float boxHeight =
            data[3 * boxes + i];
        // 保存当前候选框得分最高的类别。
        int bestClass = -1;
        // 保存当前候选框最高的类别分数。
        float bestScore = 0.0f;
        // 遍历8个类别。
        for (int classId = 0; classId < static_cast<int>(kClassNames.size());++classId)
        {
            // 类别分数从第4个通道开始。
            // classId=0时读取第4通道；
            const float score =
                data[(4 + classId) * boxes + i];

            // 找出8个类别中得分最高的类别。
            if (score > bestScore)
            {
                bestScore = score;
                bestClass = classId;
            }
        }
        // 最高分仍然低于置信度阈值时，
        // 丢弃当前候选框。
        if (bestScore < config_.confThreshold)
        {
            continue;
        }
        // YOLO输出的是中心点坐标和宽高：
        // 将其转换为左上角和右下角坐标。
        float x1 =
            cx - boxWidth * 0.5f;

        float y1 =
            cy - boxHeight * 0.5f;

        float x2 =
            cx + boxWidth * 0.5f;

        float y2 =
            cy + boxHeight * 0.5f;
        // 去掉letterbox增加的灰边。
        x1 -= info.padX;
        y1 -= info.padY;
        x2 -= info.padX;
        y2 -= info.padY;
        // 除以缩放比例，还原到原图坐标。
        x1 /= info.scale;
        y1 /= info.scale;
        x2 /= info.scale;
        y2 /= info.scale;
        // 将坐标限制在原图范围内。
        x1 = std::clamp(
            x1,
            0.0f,
            static_cast<float>(imageSize.width - 1));
        y1 = std::clamp(
            y1,
            0.0f,
            static_cast<float>(imageSize.height - 1));
        x2 = std::clamp(
            x2,
            0.0f,
            static_cast<float>(imageSize.width - 1));
        y2 = std::clamp(
            y2,
            0.0f,
            static_cast<float>(imageSize.height - 1));
        // 用左上角和右下角创建OpenCV矩形框。
        const cv::Rect rectangle( cv::Point(static_cast<int>(x1),static_cast<int>(y1)),
        cv::Point(static_cast<int>(x2),static_cast<int>(y2)));
        // 面积小于或等于0说明矩形无效。
        if (rectangle.area() <= 0)
        {
            continue;
        }
        candidateBoxes.push_back(rectangle);      //保存候选框
        candidateScores.push_back(bestScore);     //保存候选框的最高类别分数
        candidateClassIds.push_back(bestClass);   //保存候选框的最高类别编号
    }

    // 创建下标数组。
    // 如果有3个候选框，内容就是：
    // order = {0, 1, 2}
    std::vector<int> order(candidateBoxes.size());
    std::iota( order.begin(),order.end(),0);
    // 按置信度从高到低排列下标。
    std::sort( order.begin(),order.end(),[&](int left, int right){return candidateScores[left] >candidateScores[right];});
    // 保存经过NMS后需要保留的候选框下标。
    std::vector<int> keep;
    // 按置信度从高到低检查候选框。
    for (int currentIndex : order)
    {
        bool suppressed = false;
        // 与已经保留的候选框进行比较。
        for (int keptIndex : keep)
        {
            // 只对相同类别执行NMS。
            const bool sameClass =candidateClassIds[currentIndex] == candidateClassIds[keptIndex];
            // 计算两个框的IoU。
            const float overlap =iou(candidateBoxes[currentIndex],candidateBoxes[keptIndex]);
            // 相同类别并且重叠程度超过阈值时，
            // 删除当前这个置信度较低的框。
            if (sameClass &&
                overlap > config_.nmsThreshold)
            {
                suppressed = true;
                break;
            }
        }
        // 没有被抑制就保留。
        if (!suppressed)
        {
            keep.push_back(currentIndex);
        }
    }
    // 将保留的候选框转换为Detection对象。
    std::vector<Detection> detections;
    for (int index : keep)
    {
        Detection detection;
        detection.classId = candidateClassIds[index];
        detection.score = candidateScores[index];
        detection.box =candidateBoxes[index];
        detection.label =kClassNames[detection.classId];
        // classId为0～2时表示豆子。
        if (isBeanClass(detection.classId))
        {
            detection.kind =TargetKind::Bean;
            detection.bean =classIdToBean(detection.classId);
        }
        // classId为3～7时表示数字箱。
        else if (isDigitClass(detection.classId))
        {
            detection.kind =TargetKind::DigitBox;

            detection.digit = classIdToDigit(detection.classId);
        }
         detections.push_back(detection);
    }
    return detections;
}