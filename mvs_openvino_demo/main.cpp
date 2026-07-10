/**
 * 迈德威视相机 + OpenVINO YOLO11 实时目标检测
 *
 * 功能：
 *   1. 使用迈德威视 SDK 采集工业相机画面
 *   2. 加载 OpenVINO IR 模型（best.xml + best.bin）进行 YOLO11 推理
 *   3. 在画面上实时绘制检测框、类别、置信度
 *   4. 按 ESC 退出
 *
 * 运行环境：Linux + OpenVINO 2024+ + OpenCV + 迈德威视 SDK
 */

#include "CameraApi.h"

#include <opencv2/opencv.hpp>
#include <openvino/openvino.hpp>

#include <stdio.h>
#include <vector>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>

// ============================================================
// 可调参数
// ============================================================
const float CONF_THRESHOLD   = 0.4f;   // 置信度阈值
const float NMS_THRESHOLD    = 0.5f;   // NMS IoU 阈值
const int   INPUT_WIDTH      = 640;    // 模型输入宽度
const int   INPUT_HEIGHT     = 640;    // 模型输入高度

// 模型路径（请根据实际位置修改）
const char* MODEL_XML = "../best_openvino_model/best.xml";

// 类别名称（与 metadata.yaml 一致）
const std::vector<std::string> CLASS_NAMES = {
    "soybean",          // 0
    "mung_bean",        // 1
    "white_kidney_bean",// 2
    "data_1",           // 3
    "data_2",           // 4
    "data_3",           // 5
    "data_4",           // 6
    "data_5"            // 7
};

// 类别显示颜色 (BGR)
const std::vector<cv::Scalar> COLORS = {
    cv::Scalar(0,   255, 0  ),   // 绿色
    cv::Scalar(255, 0,   0  ),   // 蓝色
    cv::Scalar(0,   0,   255),   // 红色
    cv::Scalar(255, 255, 0  ),   // 青色
    cv::Scalar(255, 0,   255),   // 品红
    cv::Scalar(0,   255, 255),   // 黄色
    cv::Scalar(128, 0,   128),   // 紫色
    cv::Scalar(255, 165, 0  )    // 橙色
};

// ============================================================
// 检测结果结构体
// ============================================================
struct Detection {
    float x1, y1, x2, y2;   // 边界框（像素坐标）
    float confidence;        // 置信度
    int   class_id;          // 类别 ID
    std::string class_name;  // 类别名称
};

// ============================================================
// 预处理：将相机帧转为模型输入 blob
// ============================================================
cv::Mat preprocess(const cv::Mat& frame) {
    // 1. BGR → RGB
    cv::Mat rgb;
    cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);

    // 2. Resize 到模型输入尺寸 (640, 640)
    cv::Mat resized;
    cv::resize(rgb, resized, cv::Size(INPUT_WIDTH, INPUT_HEIGHT));

    // 3. 转为 float32 并归一化到 [0, 1]
    cv::Mat blob;
    resized.convertTo(blob, CV_32F, 1.0 / 255.0);

    return blob;
}

// ============================================================
// 将 HWC 图像封装成 OpenVINO tensor 所需的 NCHW 格式
// ============================================================
ov::Tensor blob_to_tensor(const cv::Mat& blob) {
    // blob: HWC (640, 640, 3), float32, [0,1]
    // 返回 ov::Tensor，内存布局 NCHW
    int h = blob.rows;
    int w = blob.cols;
    int c = blob.channels();

    ov::Shape shape = {1, static_cast<size_t>(c), static_cast<size_t>(h), static_cast<size_t>(w)};
    ov::Tensor tensor(ov::element::f32, shape);

    float* data = tensor.data<float>();

    // HWC → CHW
    for (int ch = 0; ch < c; ++ch) {
        for (int row = 0; row < h; ++row) {
            const float* src = blob.ptr<float>(row) + ch;
            float* dst = data + ch * h * w + row * w;
            for (int col = 0; col < w; ++col) {
                dst[col] = *src;
                src += c;
            }
        }
    }

    return tensor;
}

// ============================================================
// 后处理：解析 YOLO11 输出，NMS，返回检测结果
// ============================================================
std::vector<Detection> postprocess(const ov::Tensor& output,
                                    int orig_w, int orig_h,
                                    float conf_threshold,
                                    float nms_threshold) {
    std::vector<Detection> detections;

    const float* out_data = output.data<float>();
    ov::Shape out_shape = output.get_shape();

    // YOLO11 输出: [1, 4 + num_classes, num_anchors]
    // 本模型 num_classes = 8, 预期 shape = [1, 12, 8400]
    if (out_shape.size() != 3) {
        fprintf(stderr, "ERROR: 模型输出维度不是3维，实际: %zu\n", out_shape.size());
        return detections;
    }

    size_t num_pred  = out_shape[1];  // 4 + num_classes = 12
    size_t num_boxes = out_shape[2];  // 8400
    size_t num_cls   = num_pred - 4;  // 8

    // 缩放因子（模型输入 640×640 → 原始图像）
    float scale_x = static_cast<float>(orig_w) / INPUT_WIDTH;
    float scale_y = static_cast<float>(orig_h) / INPUT_HEIGHT;

    // 遍历所有 anchor，筛选高置信度候选框
    std::vector<Detection> candidates;
    for (size_t i = 0; i < num_boxes; ++i) {
        // 找到最高分类置信度
        float max_cls_conf = 0.0f;
        int   best_cls_id   = -1;
        for (size_t c = 0; c < num_cls; ++c) {
            float score = out_data[(4 + c) * num_boxes + i];
            if (score > max_cls_conf) {
                max_cls_conf = score;
                best_cls_id  = static_cast<int>(c);
            }
        }

        if (max_cls_conf < conf_threshold) continue;

        // 解析 cx, cy, w, h
        float cx = out_data[0 * num_boxes + i];
        float cy = out_data[1 * num_boxes + i];
        float w  = out_data[2 * num_boxes + i];
        float h  = out_data[3 * num_boxes + i];

        // 中心点 → 左上/右下角，并缩放到原始图像坐标
        float x1 = (cx - w / 2.0f) * scale_x;
        float y1 = (cy - h / 2.0f) * scale_y;
        float x2 = (cx + w / 2.0f) * scale_x;
        float y2 = (cy + h / 2.0f) * scale_y;

        // 边界裁剪
        x1 = std::max(0.0f, std::min(x1, static_cast<float>(orig_w)));
        y1 = std::max(0.0f, std::min(y1, static_cast<float>(orig_h)));
        x2 = std::max(0.0f, std::min(x2, static_cast<float>(orig_w)));
        y2 = std::max(0.0f, std::min(y2, static_cast<float>(orig_h)));

        if (x2 <= x1 || y2 <= y1) continue;

        Detection det;
        det.x1         = x1;
        det.y1         = y1;
        det.x2         = x2;
        det.y2         = y2;
        det.confidence = max_cls_conf;
        det.class_id   = best_cls_id;
        det.class_name = (best_cls_id < static_cast<int>(CLASS_NAMES.size()))
                             ? CLASS_NAMES[best_cls_id]
                             : "unknown";
        candidates.push_back(det);
    }

    // NMS：按置信度降序排序
    std::sort(candidates.begin(), candidates.end(),
              [](const Detection& a, const Detection& b) {
                  return a.confidence > b.confidence;
              });

    std::vector<bool> removed(candidates.size(), false);
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (removed[i]) continue;
        detections.push_back(candidates[i]);

        for (size_t j = i + 1; j < candidates.size(); ++j) {
            if (removed[j]) continue;

            // 计算 IoU
            float inter_x1 = std::max(candidates[i].x1, candidates[j].x1);
            float inter_y1 = std::max(candidates[i].y1, candidates[j].y1);
            float inter_x2 = std::min(candidates[i].x2, candidates[j].x2);
            float inter_y2 = std::min(candidates[i].y2, candidates[j].y2);

            float inter_w = std::max(0.0f, inter_x2 - inter_x1);
            float inter_h = std::max(0.0f, inter_y2 - inter_y1);
            float inter_area = inter_w * inter_h;

            float area_i = (candidates[i].x2 - candidates[i].x1) *
                           (candidates[i].y2 - candidates[i].y1);
            float area_j = (candidates[j].x2 - candidates[j].x1) *
                           (candidates[j].y2 - candidates[j].y1);
            float union_area = area_i + area_j - inter_area;

            if (union_area > 0.0f && inter_area / union_area > nms_threshold) {
                removed[j] = true;
            }
        }
    }

    return detections;
}

// ============================================================
// 在图像上绘制检测结果
// ============================================================
void draw_detections(cv::Mat& frame, const std::vector<Detection>& detections) {
    for (const auto& det : detections) {
        int cls_id = det.class_id % COLORS.size();
        cv::Scalar color = COLORS[cls_id];

        // 画框
        cv::Point pt1(static_cast<int>(det.x1), static_cast<int>(det.y1));
        cv::Point pt2(static_cast<int>(det.x2), static_cast<int>(det.y2));
        cv::rectangle(frame, pt1, pt2, color, 2);

        // 标签文字
        char label[128];
        snprintf(label, sizeof(label), "%s %.2f", det.class_name.c_str(), det.confidence);

        int baseline = 0;
        cv::Size text_size = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX,
                                             0.5, 1, &baseline);

        // 标签背景
        cv::rectangle(frame,
                      cv::Point(pt1.x, pt1.y - text_size.height - baseline - 4),
                      cv::Point(pt1.x + text_size.width, pt1.y),
                      color, cv::FILLED);

        // 标签文字（白色）
        cv::putText(frame, label,
                    cv::Point(pt1.x, pt1.y - baseline - 2),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5,
                    cv::Scalar(255, 255, 255), 1);
    }
}

// ============================================================
// 打印模型输入/输出信息（调试用）
// ============================================================
void print_model_info(const ov::CompiledModel& compiled_model) {
    auto inputs = compiled_model.inputs();
    auto outputs = compiled_model.outputs();

    printf("\n===== 模型信息 =====\n");
    for (const auto& in : inputs) {
        // get_names() 返回 set，为空时用 "?" 代替
        auto names = in.get_names();
        const char* name_str = names.empty() ? "?" : names.begin()->c_str();
        printf("输入名称: %s, 形状: ", name_str);
        for (auto dim : in.get_partial_shape()) {
            if (dim.is_dynamic())
                printf("? ");
            else
                printf("%ld ", dim.get_length());
        }
        printf("\n");
    }
    for (const auto& out : outputs) {
        auto names = out.get_names();
        const char* name_str = names.empty() ? "?" : names.begin()->c_str();
        printf("输出名称: %s, 形状: ", name_str);
        for (auto dim : out.get_partial_shape()) {
            if (dim.is_dynamic())
                printf("? ");
            else
                printf("%ld ", dim.get_length());
        }
        printf("\n");
    }
    printf("=====================\n\n");
}

// ============================================================
// 主函数
// ============================================================
int main(int argc, char** argv) {
    // ---------- 1. 初始化 OpenVINO ----------
    printf("[INFO] 初始化 OpenVINO...\n");

    // 尝试从命令行参数读取模型路径
    std::string model_path = MODEL_XML;
    if (argc > 1) {
        model_path = argv[1];
    } else {
        // 尝试多个可能的默认路径
        std::ifstream test1("../best_openvino_model/best.xml");
        std::ifstream test2("./best_openvino_model/best.xml");
        std::ifstream test3("best_openvino_model/best.xml");
        if (!test1.good() && !test2.good() && !test3.good()) {
            printf("[WARN] 未找到模型文件，请通过命令行参数指定:\n");
            printf("      %s /path/to/best.xml\n", argv[0]);
            printf("      尝试的默认路径:\n");
            printf("        ../best_openvino_model/best.xml\n");
            printf("        ./best_openvino_model/best.xml\n");
            printf("        best_openvino_model/best.xml\n");
        }
        // 用第一个存在的
        if (test2.good()) model_path = "./best_openvino_model/best.xml";
        if (test3.good()) model_path = "best_openvino_model/best.xml";
    }

    printf("[INFO] 模型路径: %s\n", model_path.c_str());

    ov::Core core;
    // 读取并编译模型
    std::shared_ptr<ov::Model> model = core.read_model(model_path);
    ov::CompiledModel compiled_model = core.compile_model(model, "AUTO");
    ov::InferRequest infer_request = compiled_model.create_infer_request();

    print_model_info(compiled_model);

    // ---------- 2. 初始化迈德威视相机 ----------
    printf("[INFO] 初始化迈德威视相机...\n");

    CameraSdkInit(1);

    int iCameraCounts = 1;
    tSdkCameraDevInfo tCameraEnumList;
    int iStatus = CameraEnumerateDevice(&tCameraEnumList, &iCameraCounts);
    printf("[INFO] 枚举状态 = %d, 相机数量 = %d\n", iStatus, iCameraCounts);

    if (iCameraCounts == 0) {
        printf("[ERROR] 未检测到相机设备！\n");
        return -1;
    }

    int hCamera = -1;
    iStatus = CameraInit(&tCameraEnumList, -1, -1, &hCamera);
    printf("[INFO] 初始化状态 = %d\n", iStatus);
    if (iStatus != CAMERA_STATUS_SUCCESS) {
        printf("[ERROR] 相机初始化失败！\n");
        return -1;
    }

    // 获取相机能力
    tSdkCameraCapbility tCapability;
    CameraGetCapability(hCamera, &tCapability);

    int maxW = tCapability.sResolutionRange.iWidthMax;
    int maxH = tCapability.sResolutionRange.iHeightMax;
    printf("[INFO] 相机最大分辨率: %d x %d\n", maxW, maxH);

    // 预分配 RGB 缓存
    cv::Mat rgbBuf(maxH, maxW, CV_8UC3);
    unsigned char* g_pRgbBuffer = rgbBuf.data;

    // 配置输出格式
    int channel = 3;
    if (tCapability.sIspCapacity.bMonoSensor) {
        channel = 1;
        CameraSetIspOutFormat(hCamera, CAMERA_MEDIA_TYPE_MONO8);
        printf("[INFO] 传感器类型: 黑白\n");
    } else {
        channel = 3;
        CameraSetIspOutFormat(hCamera, CAMERA_MEDIA_TYPE_BGR8);
        printf("[INFO] 传感器类型: 彩色\n");
    }

    // 启动相机数据流
    CameraPlay(hCamera);

    // ---------- 3. FPS 统计变量 ----------
    auto last_time = std::chrono::steady_clock::now();
    int frame_count = 0;
    float fps_display = 0.0f;

    // ---------- 4. 主循环：取图 → 推理 → 显示 ----------
    printf("\n[INFO] 开始实时检测，按 ESC 退出...\n\n");

    tSdkFrameHead sFrameInfo;
    BYTE* pbyBuffer = nullptr;

    while (true) {
        // 4a. 从相机获取一帧
        if (CameraGetImageBuffer(hCamera, &sFrameInfo, &pbyBuffer, 1000)
            != CAMERA_STATUS_SUCCESS) {
            continue;  // 超时则重试
        }

        // 4b. SDK 图像处理（Bayer → BGR）
        CameraImageProcess(hCamera, pbyBuffer, g_pRgbBuffer, &sFrameInfo);

        // 4c. 构造 OpenCV Mat（零拷贝）
        int type = (sFrameInfo.uiMediaType == CAMERA_MEDIA_TYPE_MONO8)
                       ? CV_8UC1
                       : CV_8UC3;
        cv::Mat frame(cv::Size(sFrameInfo.iWidth, sFrameInfo.iHeight),
                      type, g_pRgbBuffer);

        // 如果是灰度图，转 BGR 以便绘制彩色框
        cv::Mat display_frame;
        if (channel == 1) {
            cv::cvtColor(frame, display_frame, cv::COLOR_GRAY2BGR);
        } else {
            display_frame = frame.clone();
        }

        // 4d. 预处理
        cv::Mat blob = preprocess(frame);

        // 4e. 预处理 + 封装为 OpenVINO NCHW tensor
        ov::Tensor input_tensor = blob_to_tensor(blob);

        // 4f. 推理
        infer_request.set_input_tensor(input_tensor);
        infer_request.infer();
        auto output_tensor = infer_request.get_output_tensor();

        // 4g. 后处理
        auto detections = postprocess(output_tensor,
                                       sFrameInfo.iWidth,
                                       sFrameInfo.iHeight,
                                       CONF_THRESHOLD,
                                       NMS_THRESHOLD);

        // 4h. 绘制检测框
        draw_detections(display_frame, detections);

        // 4i. FPS 计算
        frame_count++;
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - last_time).count();
        if (elapsed >= 1.0f) {
            fps_display = frame_count / elapsed;
            last_time = now;
            frame_count = 0;
        }

        // 4j. 显示 FPS 和检测数量
        char info[128];
        snprintf(info, sizeof(info), "FPS: %.1f | Detections: %zu",
                 fps_display, detections.size());
        cv::putText(display_frame, info,
                    cv::Point(10, 30),
                    cv::FONT_HERSHEY_SIMPLEX, 0.8,
                    cv::Scalar(0, 255, 0), 2);

        // 4k. 显示画面
        cv::imshow("Real-time Detection (ESC to exit)", display_frame);

        // 4l. 释放帧缓存
        CameraReleaseImageBuffer(hCamera, pbyBuffer);

        // 4m. 按键检测
        char c = static_cast<char>(cv::waitKey(1));
        if (c == 27) {  // ESC
            printf("[INFO] 检测到 ESC，停止运行\n");
            break;
        }
    }

    // ---------- 5. 清理资源 ----------
    CameraUnInit(hCamera);
    cv::destroyAllWindows();
    printf("[INFO] 程序正常退出\n");

    return 0;
}
