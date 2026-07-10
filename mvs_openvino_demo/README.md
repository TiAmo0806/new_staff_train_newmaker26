# 迈德威视相机 + OpenVINO YOLO11 实时目标检测

## 项目概述

本项目整合 **迈德威视工业相机 SDK** 与 **OpenVINO 推理框架**，实现：
- 从工业相机实时采集图像
- 加载 YOLO11 OpenVINO IR 模型进行 8 类豆类目标检测
- 在画面中实时绘制检测框、类别名、置信度
- 实时显示 FPS

---

## 目录

- [项目结构](#项目结构)
- [模块架构说明](#模块架构说明)
- [依赖与安装](#依赖与安装)
- [快速开始](#快速开始)
- [main.cpp 逐行详解](#maincpp-逐行详解)
- [各模块源码详解](#各模块源码详解)
- [CMakeLists.txt 详解](#cmakeliststxt-详解)
- [build.sh 一键编译脚本详解](#buildsh-一键编译脚本详解)
- [对话中踩过的坑与解决方案](#对话中踩过的坑与解决方案)
- [常见问题](#常见问题)

---

## 项目结构

```
mvs_openvino_demo/
├── src/
│   ├── main.cpp             # 主入口 — 只负责串联调用，~70 行
│   ├── camera.hpp / camera.cpp         # 相机模块
│   ├── preprocess.hpp / preprocess.cpp # 预处理模块
│   ├── detector.hpp / detector.cpp     # 推理模块
│   └── visualize.hpp / visualize.cpp   # 绘制模块
├── CMakeLists.txt            # CMake 编译配置
├── build.sh                  # 一键编译脚本
├── build/                    # 编译输出目录（自动生成）
│   ├── openvino_links/       # OpenVINO .so 符号链接（自动生成）
│   └── mvs_openvino_demo     # 编译产物（可执行文件）
└── README.md                 # 本文件
```

---

## 模块架构说明

代码按功能拆分为 **4 个独立模块 + 1 个主入口**，遵循"单一职责原则"。

```
┌──────────────────────────────────────────────────┐
│  main.cpp（主入口 / 调度者）                       │
│                                                    │
│  1. 创建 Camera、Detector 对象                    │
│  2. 循环：取帧 → 推理 → 绘制 → 显示               │
│  3. 管理 FPS 统计和按键退出                       │
└────┬──────────┬──────────┬───────────────────────┘
     │          │          │
     ▼          ▼          ▼
┌─────────┐ ┌──────────┐ ┌─────────────┐
│ Camera  │ │ Detector │ │ visualize   │
│ 相机模块 │ │ 推理模块  │ │ 绘制模块     │
└────┬────┘ └────┬─────┘ └─────────────┘
     │           │
     ▼           ▼
  camera      ┌──────────┐
   SDK        │preprocess│
              │ 预处理    │
              └──────────┘
```

### 各模块职责一览

| 模块 | 文件 | 负责 | 对外接口 |
|------|------|------|---------|
| **Camera** | `camera.hpp/cpp` | 相机枚举、初始化、取帧、释放 | `open()`, `read()`, `release()`, `width()`, `height()`, `isMono()` |
| **preprocess** | `preprocess.hpp/cpp` | BGR→RGB、Resize、归一化、HWC→CHW | `preprocess(frame)`, `blob_to_tensor(blob)` |
| **Detector** | `detector.hpp/cpp` | 模型加载、推理、后处理、NMS | `load(path)`, `detect(frame)`, `printModelInfo()` |
| **visualize** | `visualize.hpp/cpp` | 画检测框、类别标签、置信度 | `drawDetections(frame, detections)`, `getClassColors()` |

### Camera 模块详解

**职责**：封装迈德威视 SDK 的所有底层操作，对外只暴露 3 个核心方法。

```cpp
// 打开相机（内部完成枚举→初始化→配置→启动流）
Camera cam;
cam.open();

// 循环取帧（内部完成取图→图像处理→零拷贝→释放缓存）
cv::Mat frame;
while (cam.read(frame)) {
    // frame 就是可直接使用的 BGR Mat
}

// 关闭释放
cam.release();
```

**封装细节**（调用方无需关心）：
- `CameraSdkInit(1)` — SDK 全局初始化
- `CameraEnumerateDevice()` — 枚举设备
- `CameraInit()` — 打开指定相机
- `CameraGetCapability()` — 读取最大分辨率
- `CameraSetIspOutFormat()` — 黑白/彩色自动适配
- `CameraPlay()` — 启动数据流
- `CameraImageProcess()` — Bayer→BGR
- `CameraReleaseImageBuffer()` — 释放帧缓存（⚠️ 忘记调用会卡死）
- `CameraUnInit()` — 关闭相机

**设计要点**：
- 用 `cv::Mat` 预分配最大分辨率缓存，取帧时零拷贝构造 Mat 视图
- 析构函数自动调用 `release()`，即使异常退出也不会泄漏资源（RAII）
- 提供 `isMono()` 接口，调用方可判断是否需要 `cvtColor(GRAY2BGR)`

### preprocess 模块详解

**职责**：将相机原始 BGR 图像转换为模型可接受的 NCHW float32 Tensor。

```cpp
// 步骤1：图像预处理（仍为 HWC 布局）
cv::Mat blob = preprocess(frame);
//    内部：BGR→RGB → Resize(640×640) → uint8→float32 → [0,1] 归一化

// 步骤2：布局转换 + 封装 Tensor
ov::Tensor tensor = blob_to_tensor(blob);
//    内部：HWC→CHW 手动搬运 → 构造 ov::Tensor(shape=[1,3,640,640])
```

**处理流水线**：
```
原始帧 (BGR, uint8, W×H×3)
  → cvtColor: BGR → RGB
  → resize: W×H → 640×640
  → convertTo: uint8[0,255] → float32[0,1]
  → HWC→CHW: 三层循环手动搬运
  → ov::Tensor: [1, 3, 640, 640]
```

### Detector 模块详解

**职责**：管理 OpenVINO 模型生命周期，提供"输入图像 → 检测结果"的一站式接口。

```cpp
Detector detector;

// 可调参数（有默认值，可按需修改）
detector.confThreshold = 0.4f;   // 置信度阈值
detector.nmsThreshold  = 0.5f;   // NMS IoU 阈值
detector.inputWidth    = 640;    // 模型输入尺寸
detector.inputHeight   = 640;

// 加载模型（自动完成：读取IR→编译→创建推理请求→打印模型信息）
detector.load("best.xml", "AUTO");  // "AUTO"=自动选CPU/GPU

// 一行推理
auto results = detector.detect(frame);
```

**`detect()` 内部流程**：
```
1. preprocess(frame)        → blob (HWC, float32, 640×640×3)
2. blob_to_tensor(blob)     → ov::Tensor (NCHW, [1,3,640,640])
3. infer_request.infer()    → 模型推理
4. postprocess(output, ...) → 解析 [1,12,8400] → 置信度筛选 → NMS
5. 返回 std::vector<Detection>
```

**`load()` 内部流程**：
```
1. core.read_model(xml)              → 解析 .xml+.bin
2. core.compile_model(model, device) → 优化编译到 CPU/GPU
3. compiled_model.create_infer_request() → 创建可复用的推理请求
4. printModelInfo()                  → 打印输入输出形状
```

### visualize 模块详解

**职责**：在图像上原地绘制检测结果，不关检测逻辑。

```cpp
drawDetections(display, detections);
```

**绘制内容**（每检测框）：
1. 彩色矩形框（颜色按类别区分，线宽 2px）
2. 标签背景（实心矩形，与框同色）
3. 标签文字（白色，格式 `"类别名 0.87"`）

**依赖关系**：visualize 只依赖 `Detection` 结构体（定义在 `detector.hpp` 中），不依赖任何 OpenVINO API。

### main.cpp 主入口

**职责**：只做"创建对象 → 串联调用 → 管理生命周期"，不含任何算法细节。

```cpp
int main() {
    // 1. 创建 + 初始化
    Camera cam;    cam.open();
    Detector det;  det.load("best.xml");

    // 2. 主循环
    while (cam.read(frame)) {
        auto results = det.detect(frame);     // 推理（一行）
        drawDetections(display, results);      // 绘制（一行）
        imshow("...", display);                // 显示（一行）
        if (waitKey(1) == 27) break;           // ESC 退出
    }

    // 3. 自动清理（析构函数）
}
```

---

## 依赖与安装

### 操作系统

- Linux（Ubuntu 20.04 / 22.04 / 24.04）

### 依赖列表

| 依赖 | 用途 | 安装命令 |
|------|------|---------|
| OpenCV 4.x | 图像处理、GUI 显示 | `sudo apt install libopencv-dev` |
| OpenVINO 2024+ | YOLO11 模型推理 | `pip install openvino` |
| 迈德威视 SDK | 工业相机驱动与采集 | 厂商提供，默认路径 `/home/hu/mvs/linuxSDK_V2.1.0.49202602041120` |
| CMake 3.14+ | 编译系统 | `sudo apt install cmake` |
| GCC 9+ (支持 C++17) | C++ 编译器 | `sudo apt install build-essential` |

### 验证安装

```bash
# OpenCV
pkg-config --modversion opencv4

# OpenVINO（确认存在 cmake 配置和头文件）
ls ~/.local/lib/python3.10/site-packages/openvino/cmake/OpenVINOConfig.cmake
ls ~/.local/lib/python3.10/site-packages/openvino/include/openvino/openvino.hpp

# 迈德威视 SDK
ls /home/hu/mvs/linuxSDK_V2.1.0.49202602041120/include/CameraApi.h
ls /home/hu/mvs/linuxSDK_V2.1.0.49202602041120/lib/x64/libMVSDK.so
```

---

## 快速开始

### 1. 编译

```bash
cd ~/mvs_openvino_demo
chmod +x build.sh
./build.sh
```

或手动编译：

```bash
cd ~/mvs_openvino_demo
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

### 2. 准备模型文件

将 YOLO11 的 OpenVINO IR 模型（`best.xml` + `best.bin`）复制到 `build` 目录：

```bash
cp /path/to/best.xml ~/mvs_openvino_demo/build/
cp /path/to/best.bin ~/mvs_openvino_demo/build/
```

### 3. 设置库路径（必须）

OpenVINO 的 `.so` 文件不在系统默认搜索路径中，需要设置 `LD_LIBRARY_PATH`：

```bash
# 临时生效（每次新开终端都要执行）
export LD_LIBRARY_PATH=$HOME/.local/lib/python3.10/site-packages/openvino/libs:$LD_LIBRARY_PATH

# 永久生效（推荐）
echo 'export LD_LIBRARY_PATH=$HOME/.local/lib/python3.10/site-packages/openvino/libs:$LD_LIBRARY_PATH' >> ~/.bashrc
source ~/.bashrc
```

### 4. 运行

```bash
cd ~/mvs_openvino_demo/build

# 模型直接放在 build 目录下
./mvs_openvino_demo ./best.xml

# 或模型在子目录中
./mvs_openvino_demo ./best_openvino_model/best.xml
```

### 5. 退出

按 `ESC` 键退出程序。

### 6. 一键启动脚本（可选）

创建 `run.sh` 避免每次手动设置环境变量：

```bash
cat > ~/mvs_openvino_demo/build/run.sh << 'EOF'
#!/bin/bash
export LD_LIBRARY_PATH=$HOME/.local/lib/python3.10/site-packages/openvino/libs:$LD_LIBRARY_PATH
cd "$(dirname "$0")"
./mvs_openvino_demo ./best.xml
EOF
chmod +x ~/mvs_openvino_demo/build/run.sh
```

以后只需 `./run.sh` 即可启动。

---

## main.cpp 逐行详解

### 1. 文件头注释（第1-11行）

```cpp
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
```

**作用**：`/** ... */` 是 C/C++ 文档注释，描述程序的整体功能和运行环境，方便自己或他人快速了解代码用途。

---

### 2. 头文件引入（第13-23行）

```cpp
#include "CameraApi.h"
```

**作用**：引入迈德威视工业相机 SDK 的 API 声明。
**写法规则**：使用双引号 `"..."` 而非尖括号 `<...>`，表示该头文件是项目第三方库，编译器先在项目目录搜索。
**定义内容**：该文件定义了所有相机操作函数（`CameraSdkInit`、`CameraEnumerateDevice`、`CameraInit`、`CameraGetImageBuffer`、`CameraImageProcess` 等）以及相关结构体（`tSdkCameraDevInfo`、`tSdkCameraCapbility`、`tSdkFrameHead` 等）。

```cpp
#include <opencv2/opencv.hpp>
```

**作用**：引入 OpenCV 全部功能模块。`opencv.hpp` 是 OpenCV 的汇总头文件，一次性包含 core、imgproc、highgui、videoio、dnn 等所有子模块。
**写法规则**：尖括号 `<...>` 表示系统标准 include 路径，CMake 通过 `find_package(OpenCV)` 自动配置。

```cpp
#include <openvino/openvino.hpp>
```

**作用**：引入 OpenVINO 2024+ 版本的 C++ API。提供核心类：
- `ov::Core` — 推理引擎核心，管理设备、加载模型
- `ov::Model` — 神经网络模型对象（网络拓扑 + 权重）
- `ov::CompiledModel` — 编译到具体设备后的可执行模型
- `ov::InferRequest` — 推理请求，持有输入/输出数据
- `ov::Tensor` — 多维张量，推理数据的载体

**安装方式**：`pip install openvino`，头文件位于 `~/.local/lib/python3.10/site-packages/openvino/include/`。

```cpp
#include <stdio.h>            // printf 打印
```

**作用**：C 标准输入输出库，提供 `printf`、`snprintf`、`fprintf` 等控制台输出函数。

```cpp
#include <vector>             // std::vector 动态数组
#include <algorithm>          // std::sort, std::max, std::min
#include <chrono>             // std::chrono::steady_clock 高精度计时
#include <fstream>            // std::ifstream 文件读取
#include <sstream>            // std::stringstream 字符串流（备用）
```

**作用**：
- `<vector>`：C++ 动态数组容器 `std::vector`，用于存储检测结果集合
- `<algorithm>`：提供 `std::sort`（排序）、`std::max`/`std::min`（取最值）等泛型算法
- `<chrono>`：C++11 高精度时间库，`std::chrono::steady_clock` 是单调递增时钟（不受系统时间调整影响），用于 FPS 计算
- `<fstream>`：文件输入流 `std::ifstream`，用于检查模型文件是否存在
- `<sstream>`：字符串流，用于格式化（备用）

---

### 3. 可调参数（第28-58行）

```cpp
const float CONF_THRESHOLD   = 0.4f;   // 置信度阈值
```

**作用**：只有分类置信度 ≥ 0.4 的检测框才被保留。值越高误检越少但可能漏检；值越低召回率越高但可能产生更多误报。
**写法规则**：`const` 编译期常量，不可修改；`0.4f` 后缀 `f` 明确表示 `float` 类型（不加 `f` 默认 `double`）。

```cpp
const float NMS_THRESHOLD    = 0.5f;   // NMS IoU 阈值
```

**作用**：非极大值抑制（NMS）的去重门槛。两个检测框的 IoU（交并比）> 0.5 时认为检测到同一目标，保留置信度高的那个。

```
IoU = 交集面积 / 并集面积，取值范围 [0, 1]，值越大两框重叠越严重
```

```cpp
const int   INPUT_WIDTH      = 640;    // 模型输入宽度
const int   INPUT_HEIGHT     = 640;    // 模型输入高度
```

**作用**：YOLO11 模型训练时使用的输入尺寸。预处理时必须将相机画面 resize 到此尺寸。

```cpp
const char* MODEL_XML = "../best_openvino_model/best.xml";
```

**作用**：模型文件默认路径（相对于可执行文件所在目录）。OpenVINO IR 模型由两个文件组成：
- `.xml` — 网络拓扑结构（层的连接关系）
- `.bin` — 权重参数（训练好的数值）

加载时只需指定 `.xml`，SDK 会自动查找同名的 `.bin`。

```cpp
const std::vector<std::string> CLASS_NAMES = {
    "soybean", "mung_bean", "white_kidney_bean",
    "data_1", "data_2", "data_3", "data_4", "data_5"
};
```

**作用**：8 个类别名称，索引 0~7 与模型输出中类别 ID 一一对应。

```cpp
const std::vector<cv::Scalar> COLORS = {
    cv::Scalar(0,   255, 0  ),   // 绿色
    cv::Scalar(255, 0,   0  ),   // 蓝色
    cv::Scalar(0,   0,   255),   // 红色
    // ...
};
```

**作用**：为 8 个类别各分配一种 BGR 颜色，用于绘制检测框和标签。
**注意**：`cv::Scalar(B, G, R)` 顺序是 BGR（不是 RGB），这是 OpenCV 默认色彩顺序。

---

### 4. Detection 结构体（第63-68行）

```cpp
struct Detection {
    float x1, y1, x2, y2;   // 边界框左上角和右下角（像素坐标）
    float confidence;        // 置信度 [0, 1]
    int   class_id;          // 类别 ID (0~7)
    std::string class_name;  // 类别名称（"soybean" 等）
};
```

**设计考量**：
- 坐标使用 `float` 而非 `int`：模型输出归一化坐标缩放后不一定是整数
- `struct` 默认成员为 public，适合纯数据容器
- 同时存储 `class_id` 和 `class_name`：避免每次绘制时查找名称

---

### 5. 预处理函数 `preprocess()`（第73-87行）

```cpp
cv::Mat preprocess(const cv::Mat& frame) {
```

**作用**：将相机采集的原始 BGR 图像转换为模型输入格式。
**参数**：`const cv::Mat&` — 常量引用，避免拷贝图像数据（一帧可能数 MB）。

```cpp
    cv::Mat rgb;
    cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
```

**作用**：BGR → RGB 色彩空间转换。OpenCV 默认读取为 BGR，而 PyTorch/OpenVINO 模型标准输入为 RGB。
**注意**：如果跳过这一步，颜色通道错乱会导致检测精度大幅下降。

```cpp
    cv::Mat resized;
    cv::resize(rgb, resized, cv::Size(INPUT_WIDTH, INPUT_HEIGHT));
```

**作用**：任意分辨率 → 640×640，匹配模型输入尺寸。默认使用双线性插值（BILINEAR）。

```cpp
    cv::Mat blob;
    resized.convertTo(blob, CV_32F, 1.0 / 255.0);
```

**作用**：`uint8` [0, 255] → `float32` [0.0, 1.0] 归一化。
**参数**：`convertTo(输出, 目标类型, alpha)` — 每个原像素值 × alpha = 新值。`CV_32F` = 32 位浮点（`float`）。

```cpp
    return blob;
```

**返回值**：640×640×3 的 float32 RGB 图像，值域 [0, 1]，内存布局 HWC。

---

### 6. 张量转换函数 `blob_to_tensor()`（第92-117行）

```cpp
ov::Tensor blob_to_tensor(const cv::Mat& blob) {
```

**作用**：将 OpenCV Mat（HWC 布局）转为 OpenVINO Tensor（NCHW 布局），通过手动内存搬运完成布局转换。
**返回类型**：`ov::Tensor` — 返回对象本身（非指针），利用 C++ 移动语义避免拷贝。

```cpp
    int h = blob.rows;       // 640
    int w = blob.cols;       // 640
    int c = blob.channels(); // 3 (RGB)
```

```cpp
    ov::Shape shape = {1, static_cast<size_t>(c), static_cast<size_t>(h), static_cast<size_t>(w)};
```

**作用**：定义 4 维形状 `[Batch=1, Channels=3, Height=640, Width=640]`（NCHW 标准）。
**写法**：`static_cast<size_t>` 安全将 `int` 转 `size_t`。

```cpp
    ov::Tensor tensor(ov::element::f32, shape);
    float* data = tensor.data<float>();
```

**作用**：创建 float32 张量并获取底层内存指针。

```cpp
    // HWC → CHW 三层循环
    for (int ch = 0; ch < c; ++ch) {
        for (int row = 0; row < h; ++row) {
            const float* src = blob.ptr<float>(row) + ch;   // 第 row 行、第 ch 通道
            float* dst = data + ch * h * w + row * w;        // CHW 中的对应位置
            for (int col = 0; col < w; ++col) {
                dst[col] = *src;
                src += c;  // 跳过同一像素的其他通道
            }
        }
    }
```

**原理图解**：

```
HWC 内存布局: [H][W][C]  → 像素连续，同像素的 RGB 相邻
     例如: R0 G0 B0 R1 G1 B1 R2 G2 B2 ...

CHW 内存布局: [C][H][W]  → 同通道的所有像素连续
     例如: R0 R1 R2 ... G0 G1 G2 ... B0 B1 B2 ...
```

**为何手动转换**：比 `cv::dnn::blobFromImage` 更灵活，且可以复用内存避免额外拷贝。

---

### 7. 后处理函数 `postprocess()`（第122-232行）

```cpp
std::vector<Detection> postprocess(const ov::Tensor& output,
                                    int orig_w, int orig_h,
                                    float conf_threshold, float nms_threshold) {
```

**参数说明**：
| 参数 | 含义 |
|------|------|
| `output` | 模型推理输出张量 |
| `orig_w, orig_h` | 相机原始帧宽高，用于坐标缩放 |
| `conf_threshold` | 置信度最低门槛 |
| `nms_threshold` | NMS 去重 IoU 阈值 |

```cpp
    const float* out_data = output.data<float>();
    ov::Shape out_shape = output.get_shape();

    if (out_shape.size() != 3) { ... return; }  // 防御性检查
```

**防御性编程**：检查张量是否为 3 维（`[1, 12, 8400]`），维度不对则打印错误并返回空结果，避免数组越界。

```cpp
    size_t num_pred  = out_shape[1];  // 12 = 4(坐标) + 8(类别)
    size_t num_boxes = out_shape[2];  // 8400 个 anchor
    size_t num_cls   = num_pred - 4;  // 8 个类别（不硬编码，适配不同模型）
```

**YOLO11 输出格式**：`[1, 4+num_classes, num_anchors]`

```
位置 0~3:  cx, cy, w, h  (中心点坐标 + 宽高，归一化到模型输入尺寸)
位置 4:    class_0 置信度 (soybean)
位置 5:    class_1 置信度 (mung_bean)
...
位置 11:   class_7 置信度 (data_5)
```

```cpp
    float scale_x = static_cast<float>(orig_w) / INPUT_WIDTH;
    float scale_y = static_cast<float>(orig_h) / INPUT_HEIGHT;
```

**作用**：模型输入 (640×640) → 原始图像坐标的缩放因子。

```cpp
    // 遍历 8400 个 anchor，找每个 anchor 的高分类别
    for (size_t i = 0; i < num_boxes; ++i) {
        float max_cls_conf = 0.0f;
        int   best_cls_id   = -1;
        for (size_t c = 0; c < num_cls; ++c) {
            float score = out_data[(4 + c) * num_boxes + i];
            if (score > max_cls_conf) { max_cls_conf = score; best_cls_id = c; }
        }
        if (max_cls_conf < conf_threshold) continue;   // 过滤低置信度
```

**内存寻址**：`out_data[(4+c) * num_boxes + i]` 定位到第 `c` 个类别、第 `i` 个 anchor 的置信度值。

```cpp
        // 读取坐标并转换：中心点 → 左上右下
        float cx = out_data[0 * num_boxes + i];
        float cy = out_data[1 * num_boxes + i];
        float w  = out_data[2 * num_boxes + i];
        float h  = out_data[3 * num_boxes + i];

        float x1 = (cx - w / 2.0f) * scale_x;  // 左边界
        float y1 = (cy - h / 2.0f) * scale_y;  // 上边界
        float x2 = (cx + w / 2.0f) * scale_x;  // 右边界
        float y2 = (cy + h / 2.0f) * scale_y;  // 下边界
```

**坐标变换**：模型输出是归一化坐标（相对 640×640），需乘以 `scale_x/y` 映射回原始帧像素坐标。

```cpp
        // 边界裁剪（clamp 到图像范围内）
        x1 = std::max(0.0f, std::min(x1, static_cast<float>(orig_w)));
        y1 = std::max(0.0f, std::min(y1, static_cast<float>(orig_h)));
        x2 = std::max(0.0f, std::min(x2, static_cast<float>(orig_w)));
        y2 = std::max(0.0f, std::min(y2, static_cast<float>(orig_h)));

        if (x2 <= x1 || y2 <= y1) continue;  // 过滤无效框（负面积）
```

```cpp
    // NMS 第一步：按置信度降序排序
    std::sort(candidates.begin(), candidates.end(),
              [](const Detection& a, const Detection& b) {
                  return a.confidence > b.confidence;  // > 表示降序
              });
```

**Lambda 表达式**：`[](参数){函数体}` 是 C++11 匿名函数，用作排序的自定义比较器。

```cpp
    // NMS 第二步：贪心保留高置信度框，抑制高重叠框
    std::vector<bool> removed(candidates.size(), false);
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (removed[i]) continue;
        detections.push_back(candidates[i]);  // 保留
        for (size_t j = i + 1; j < candidates.size(); ++j) {
            if (removed[j]) continue;
            // 计算两个矩形框的 IoU...
            if (iou > nms_threshold) removed[j] = true;  // 重叠过高则抑制
        }
    }
```

**NMS 算法复杂度**：O(n²)，实际中候选框数量经置信度过滤后通常很小。

```cpp
    // IoU 计算核心
    float inter_x1 = std::max(candidates[i].x1, candidates[j].x1);  // 交集左上 x
    float inter_y1 = std::max(candidates[i].y1, candidates[j].y1);  // 交集左上 y
    float inter_x2 = std::min(candidates[i].x2, candidates[j].x2);  // 交集右下 x
    float inter_y2 = std::min(candidates[i].y2, candidates[j].y2);  // 交集右下 y
    float inter_area = std::max(0.0f, inter_x2 - inter_x1) *
                       std::max(0.0f, inter_y2 - inter_y1);         // 交集面积
    float area_i = (box_i.x2 - box_i.x1) * (box_i.y2 - box_i.y1);  // 框 i 面积
    float area_j = (box_j.x2 - box_j.x1) * (box_j.y2 - box_j.y1);  // 框 j 面积
    float union_area = area_i + area_j - inter_area;                // 并集面积（容斥原理）
    if (union_area > 0.0f && inter_area / union_area > nms_threshold) // IoU > 阈值
```

---

### 8. 绘制函数 `draw_detections()`（第237-267行）

```cpp
void draw_detections(cv::Mat& frame, const std::vector<Detection>& detections) {
```

**参数**：`cv::Mat& frame` — 传引用，直接修改原图（无拷贝开销）。

```cpp
    int cls_id = det.class_id % COLORS.size();
    cv::Scalar color = COLORS[cls_id];
```

**作用**：用类别 ID 取对应颜色，`% COLORS.size()` 防止越界。

```cpp
    cv::rectangle(frame, pt1, pt2, color, 2);  // 画检测框，线宽 2px
```

```cpp
    char label[128];
    snprintf(label, sizeof(label), "%s %.2f", det.class_name.c_str(), det.confidence);
    // 例如: "soybean 0.87"
```

**注意**：`snprintf` 比 `sprintf` 安全，限制最大写入长度防止缓冲区溢出。

```cpp
    cv::Size text_size = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseline);
    cv::rectangle(frame, ..., color, cv::FILLED);  // 标签背景（实心矩形）
    cv::putText(frame, label, ..., cv::Scalar(255,255,255), 1);  // 白色标签文字
```

**绘制顺序**：先画实心背景矩形 → 再画白色文字，确保标签在任何背景上都可读。

---

### 9. 模型信息打印函数 `print_model_info()`（第272-303行）

```cpp
void print_model_info(const ov::CompiledModel& compiled_model) {
    auto inputs = compiled_model.inputs();    // 所有输入节点
    auto outputs = compiled_model.outputs();  // 所有输出节点
```

```cpp
    auto names = in.get_names();
    const char* name_str = names.empty() ? "?" : names.begin()->c_str();
```

**关键修复**：使用 `get_names()`（返回 `unordered_set`）而非 `get_any_name()`（抛异常）。

> ⚠️ `get_any_name()` 在节点没有名字时会抛出 `ov::AssertFailure` 异常，导致程序崩溃。必须先用 `get_names()` 检查是否为空。

```cpp
    for (auto dim : in.get_partial_shape()) {
        if (dim.is_dynamic()) printf("? ");     // 动态维度（如 batch）
        else printf("%ld ", dim.get_length());   // 静态维度
    }
```

**作用**：遍历每个维度，动态维度打印 `?`，静态维度打印具体数值。

---

### 10. 主函数 `main()`（第308-490行）

#### 10.1 模型路径解析（第313-332行）

```cpp
    std::string model_path = MODEL_XML;
    if (argc > 1) {
        model_path = argv[1];          // 命令行参数优先
    }
```

**作用**：`argv[1]` 是第一个命令行参数（如 `./best.xml`），有则用；无则尝试多个默认路径。

```cpp
    std::ifstream test2("./best_openvino_model/best.xml");
    if (test2.good()) model_path = "./best_openvino_model/best.xml";
```

**作用**：`std::ifstream::good()` 检查文件是否存在，存在则自动使用该路径。

#### 10.2 OpenVINO 初始化（第336-340行）

```cpp
    ov::Core core;
    std::shared_ptr<ov::Model> model = core.read_model(model_path);
    ov::CompiledModel compiled_model = core.compile_model(model, "AUTO");
    ov::InferRequest infer_request = compiled_model.create_infer_request();
```

**四步初始化流程**：

| 步骤 | API | 作用 |
|------|-----|------|
| 1 | `ov::Core core` | 创建推理引擎核心，管理所有硬件设备 |
| 2 | `core.read_model(path)` | 从 IR 文件读取模型（自动读 .xml + .bin） |
| 3 | `core.compile_model(model, "AUTO")` | 编译到设备。`"AUTO"` = 自动选最优（GPU > CPU > NPU），不可用时自动回退 |
| 4 | `create_infer_request()` | 创建推理请求对象，后续推理时复用 |

#### 10.3 相机初始化（第347-392行）

```cpp
    CameraSdkInit(1);       // SDK 全局初始化，参数 1 = 开启调试日志
```

```cpp
    int iCameraCounts = 1;
    tSdkCameraDevInfo tCameraEnumList;
    int iStatus = CameraEnumerateDevice(&tCameraEnumList, &iCameraCounts);
```

**作用**：枚举连接电脑的工业相机。`iCameraCounts` 是输入输出参数——输入期待的最大相机数，输出实际检测到的相机数。

```cpp
    if (iCameraCounts == 0) { return -1; }  // 无相机则退出
```

```cpp
    iStatus = CameraInit(&tCameraEnumList, -1, -1, &hCamera);
```

**参数说明**：
- 第一个 `-1`：使用默认通道（单目相机）
- 第二个 `-1`：使用出厂默认分辨率
- `&hCamera`：输出参数，获取相机句柄（后续所有操作都需要传入）

```cpp
    CameraGetCapability(hCamera, &tCapability);
    int maxW = tCapability.sResolutionRange.iWidthMax;
    int maxH = tCapability.sResolutionRange.iHeightMax;
```

**作用**：读取最大分辨率，用于预分配图像缓存。

```cpp
    cv::Mat rgbBuf(maxH, maxW, CV_8UC3);
    unsigned char* g_pRgbBuffer = rgbBuf.data;
```

**设计精髓**：用 OpenCV Mat 代替 C 的 `malloc`，利用 RAII 机制自动管理内存。将 `data` 指针传给 SDK，SDK 处理后直接写入 Mat 内存空间——零拷贝。

```cpp
    if (tCapability.sIspCapacity.bMonoSensor) {
        channel = 1;
        CameraSetIspOutFormat(hCamera, CAMERA_MEDIA_TYPE_MONO8);  // 黑白传感器
    } else {
        channel = 3;
        CameraSetIspOutFormat(hCamera, CAMERA_MEDIA_TYPE_BGR8);   // 彩色传感器
    }
    CameraPlay(hCamera);  // 启动相机数据流，必须在取帧前调用
```

#### 10.4 主循环（第405-482行）

```cpp
    while (true) {  // 无限循环，由 ESC 键 break 退出
```

```cpp
        if (CameraGetImageBuffer(hCamera, &sFrameInfo, &pbyBuffer, 1000)
            != CAMERA_STATUS_SUCCESS) { continue; }
```

**作用**：阻塞式取帧，超时时间 1000ms。失败时 `continue` 重试，不会因偶发丢帧而崩溃。

```cpp
        CameraImageProcess(hCamera, pbyBuffer, g_pRgbBuffer, &sFrameInfo);
```

**作用**：SDK 内部图像处理（Bayer 去马赛克 → BGR/灰度），输出写入 `g_pRgbBuffer`。

```cpp
        cv::Mat frame(cv::Size(sFrameInfo.iWidth, sFrameInfo.iHeight),
                      type, g_pRgbBuffer);
```

**零拷贝构造**：Mat 构造函数最后一个参数是指向已有内存的指针，不拷贝数据——只创建"视图"。

```cpp
        cv::Mat display_frame;
        if (channel == 1) {
            cv::cvtColor(frame, display_frame, cv::COLOR_GRAY2BGR);
        } else {
            display_frame = frame.clone();  // clone() 完整拷贝，用于绘制标注
        }
```

**为何 clone**：推理需要原图，标注需要修改图——必须分开，否则标注污染推理输入。

```cpp
        cv::Mat blob = preprocess(frame);                  // 预处理
        ov::Tensor input_tensor = blob_to_tensor(blob);    // HWC→CHW
        infer_request.set_input_tensor(input_tensor);      // 设置输入
        infer_request.infer();                              // 同步推理
        auto output_tensor = infer_request.get_output_tensor(); // 获取输出
```

**推理三步**：`set_input_tensor()` → `infer()` → `get_output_tensor()`。

```cpp
        auto detections = postprocess(output_tensor, ...);  // 解析结果
        draw_detections(display_frame, detections);          // 绘制
```

```cpp
    // FPS 统计
    frame_count++;
    auto now = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float>(now - last_time).count();
    if (elapsed >= 1.0f) {       // 每秒更新一次 FPS
        fps_display = frame_count / elapsed;
        last_time = now;
        frame_count = 0;
    }
```

```cpp
    cv::imshow("Real-time Detection (ESC to exit)", display_frame);
```

```cpp
    CameraReleaseImageBuffer(hCamera, pbyBuffer);  // ⚠️ 必须释放！否则相机卡死
```

```cpp
    char c = static_cast<char>(cv::waitKey(1));  // 等待 1ms，同时刷新 GUI
    if (c == 27) { break; }  // 27 = ESC 键 ASCII 码
```

**注意**：`cv::waitKey()` 是 OpenCV GUI 事件循环的必要调用——不调它窗口无法刷新显示。

#### 10.5 清理（第484-489行）

```cpp
    CameraUnInit(hCamera);       // 关闭相机
    cv::destroyAllWindows();     // 销毁所有 OpenCV 窗口
    return 0;                    // 0 = 正常退出（Unix 惯例）
```

---

## CMakeLists.txt 详解

### 每一行的作用

```cmake
cmake_minimum_required(VERSION 3.14)
```

**作用**：声明所需最低 CMake 版本。设为 3.14 是为了兼容 `find_package(OpenVINO)` 等现代特性。
**规则**：过低可能缺少新特性，过高可能不兼容旧系统。

```cmake
project(mvs_openvino_demo)
```

**作用**：定义项目名。CMake 自动设置 `${PROJECT_NAME}`、`${PROJECT_SOURCE_DIR}` 等变量。

```cmake
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
```

**作用**：强制使用 C++17 标准。`REQUIRED ON` 表示编译器不支持则报错（不自动降级）。

```cmake
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})
```

**作用**：查找已安装的 OpenCV。`REQUIRED` = 找不到则中止 cmake。`include_directories` 将头文件路径加入编译器的 `-I` 搜索列表。

```cmake
set(OPENVINO_ROOT "$ENV{HOME}/.local/lib/python3.10/site-packages/openvino")
include_directories(${OPENVINO_ROOT}/include)
```

**作用**：直接指定 OpenVINO pip 安装路径，将头文件目录加入编译器搜索路径。`$ENV{HOME}` 读取环境变量 `$HOME`。

```cmake
set(OPENVINO_LIB_DIR ${OPENVINO_ROOT}/libs)
set(OPENVINO_LINK_DIR ${CMAKE_BINARY_DIR}/openvino_links)
```

```cmake
file(MAKE_DIRECTORY ${OPENVINO_LINK_DIR})
file(GLOB OPENVINO_SO_FILES "${OPENVINO_LIB_DIR}/libopenvino.so.*")
if(OPENVINO_SO_FILES)
    list(GET OPENVINO_SO_FILES 0 OPENVINO_SO_FILE)
    execute_process(COMMAND ln -sf ${OPENVINO_SO_FILE} ${OPENVINO_LINK_DIR}/libopenvino.so)
endif()
```

**作用**：解决 pip 安装的 .so 带版本号（`libopenvino.so.2621`）导致 `-lopenvino` 失败的问题。
**流程**：
1. `file(GLOB ...)` 匹配 `libopenvino.so.*` 文件
2. `ln -sf` 创建符号链接 `libopenvino.so` → `libopenvino.so.2621`
3. `link_directories` 将链接目录加入搜索路径

```cmake
    execute_process(COMMAND ln -sf ${OV_C_FILE} ${OPENVINO_LINK_DIR}/libopenvino_c.so)
```

**作用**：同理为 `libopenvino_c.so.2621` 创建符号链接。

```cmake
link_directories(${OPENVINO_LINK_DIR} ${OPENVINO_LIB_DIR})
```

**作用**：将两个目录加入链接器搜索路径——`openvino_links/`（符号链接）和 `libs/`（运行时库）。

```cmake
set(MV_SDK_ROOT "/home/hu/mvs/linuxSDK_V2.1.0.49202602041120"
    CACHE PATH "MindVision SDK root directory")
include_directories(${MV_SDK_ROOT}/include)
```

**作用**：设置迈德威视 SDK 路径。`CACHE PATH` 允许用户在 cmake GUI 或命令行修改该值。

```cmake
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(MV_LIB_DIR ${MV_SDK_ROOT}/lib/x64)
else()
    set(MV_LIB_DIR ${MV_SDK_ROOT}/lib/x86)
endif()
link_directories(${MV_LIB_DIR})
```

**作用**：64 位系统自动选 `lib/x64`，32 位选 `lib/x86`。

```cmake
add_executable(${PROJECT_NAME} src/main.cpp)
```

**作用**：声明可执行文件构建目标。源文件在 `src/main.cpp`（而非默认的 `main.cpp`）。

```cmake
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${OpenCV_LIBS}
    openvino
    MVSDK
    pthread
    dl
)
```

**参数详解**：

| 库 | 作用 |
|----|------|
| `${OpenCV_LIBS}` | OpenCV 全部必要库（find_package 自动填充） |
| `openvino` | OpenVINO 运行时库（-lopenvino） |
| `MVSDK` | 迈德威视 SDK 动态库 `libMVSDK.so` |
| `pthread` | POSIX 线程库，SDK 内部多线程需要 |
| `dl` | 动态链接库 `libdl.so`，提供 `dlopen`/`dlsym` |

`PRIVATE` 语义：这些库仅用于链接当前目标，不传递给依赖者。

---

## build.sh 一键编译脚本详解

```bash
#!/bin/bash
```

**规则**：Shebang 必须是文件第一行第一个字节，声明由 `/bin/bash` 解释执行。不写则默认用 `/bin/sh`（可能是 dash），bash 特有语法可能报错。

```bash
set -e
```

**作用**：任何命令返回非零退出码（失败），脚本立即终止。
**重要性**：如果 cmake 配置失败，继续 make 毫无意义且产生误导性错误。

```bash
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
```

**逐步拆解**：
1. `$0` — 脚本自身路径（可能是 `./build.sh`）
2. `dirname "$0"` — 取所在目录
3. `cd "$(dirname "$0")"` — 切换到脚本目录
4. `&& pwd` — cd 成功则打印绝对路径
5. `$(...)` — 命令替换，将输出赋给变量

**目的**：无论用户在哪个目录执行脚本，都获得正确的项目绝对路径。

```bash
BUILD_DIR="${PROJECT_DIR}/build"
mkdir -p "${BUILD_DIR}"
```

`-p` 参数：目录已存在时不报错，父目录不存在时自动创建。

```bash
if [ -n "$1" ]; then
    CMAKE_ARGS="-DOpenVINO_DIR=$1"
fi
```

- `$1`：第一个命令行参数
- `[ -n "$1" ]`：`-n` 测试字符串是否非空
- `if ... then ... fi`：bash 条件判断结构
- **注意**：`[` 和 `]` 前后必须有空格

```bash
cmake .. ${CMAKE_ARGS}
```

`..` 表示 CMakeLists.txt 在上一级目录。`${CMAKE_ARGS}` 为空时展开为空字符串，不影响命令。

```bash
make -j$(nproc)
```

- `-j`：并行编译任务数
- `$(nproc)`：获取 CPU 核心数
- 8 核系统等价于 `make -j8`

---

## 对话中踩过的坑与解决方案

这是在开发过程中实际遇到的问题及修复记录：

### 坑 1：`#include <openvino/openvino.hpp>` 找不到

**现象**：
```
fatal error: openvino/openvino.hpp: 没有那个文件或目录
```

**原因**：虚拟机未安装 OpenVINO，编译器无法在标准路径找到该头文件。

**解决**：
```bash
pip install openvino
# 确认安装位置
ls ~/.local/lib/python3.10/site-packages/openvino/include/openvino/openvino.hpp
```

然后在 `CMakeLists.txt` 中直接指定路径：
```cmake
set(OPENVINO_ROOT "$ENV{HOME}/.local/lib/python3.10/site-packages/openvino")
include_directories(${OPENVINO_ROOT}/include)
```

---

### 坑 2：`find_package(OpenVINO)` 行为不可靠

**现象**：CMake 能找到 `OpenVINOConfig.cmake`，但 include 目录没有正确传递给编译器。

**原因**：`find_package` 依赖 cmake 配置文件的内部实现，pip 安装版的 target 名称和变量设置可能与标准不同。

**解决**：放弃 `find_package`，改用 `find_path` 直接定位 `openvino.hpp` 物理路径，然后显式 `include_directories`。最终确认路径后直接硬编码：
```cmake
include_directories(${OPENVINO_ROOT}/include)
link_directories(${OPENVINO_ROOT}/libs)
```

---

### 坑 3：`set_input_tensor()` API 类型不匹配

**现象**：
```
error: no matching function for call to 'ov::InferRequest::set_input_tensor(std::shared_ptr<ov::Tensor>&)'
note: candidate: 'void ov::InferRequest::set_input_tensor(const ov::Tensor&)'
note: no known conversion for argument 1 from 'std::shared_ptr<ov::Tensor>' to 'const ov::Tensor&'
```

**原因**：`blob_to_tensor` 返回 `std::shared_ptr<ov::Tensor>`，但 API 需要 `const ov::Tensor&`（对象引用）。

**解决**：将 `blob_to_tensor` 返回类型从 `std::shared_ptr<ov::Tensor>` 改为 `ov::Tensor`（值返回，利用 C++ 移动语义），内部从 `std::make_shared` 改为直接构造。

---

### 坑 4：`compiled_model.input()` 不存在

**现象**：编译错误 — `input()` 单数形式不是合法 API。

**原因**：OpenVINO 2024+ API 只有复数形式 `inputs()` 返回 `vector`，需要用索引访问。

**解决**：
```cpp
// ❌ 错误
compiled_model.input().get_any_name()

// ✅ 正确
compiled_model.inputs()[0].get_any_name()
compiled_model.outputs()[0].get_any_name()
```

---

### 坑 5：`get_any_name()` 在无名节点上抛异常

**现象**：
```
terminate called after throwing an instance of 'ov::AssertFailure'
what(): Check '!get_names().empty()' failed
Attempt to get a name for a Tensor without names
```

**原因**：YOLO11 导出的 IR 模型输出节点可能没有分配名称，调用 `get_any_name()` 直接抛异常崩溃。

**解决**：先调用 `get_names()`（返回 `unordered_set`，可空），再判断：
```cpp
// ❌ 直接调用会崩溃
in.get_any_name()

// ✅ 安全写法
auto names = in.get_names();
const char* name_str = names.empty() ? "?" : names.begin()->c_str();
```

---

### 坑 6：链接器找不到 `-lopenvino`

**现象**：
```
/usr/bin/ld: 找不到 -lopenvino: 没有那个文件或目录
```

**原因**：pip 安装的库文件名带版本后缀（`libopenvino.so.2621`），而 `-lopenvino` 只搜索 `libopenvino.so`（无后缀）或 `libopenvino.a`。

**解决**：在 CMake 中通过 `ln -sf` 创建符号链接：
```cmake
execute_process(COMMAND ln -sf ${OPENVINO_SO_FILE} ${OPENVINO_LINK_DIR}/libopenvino.so)
link_directories(${OPENVINO_LINK_DIR} ${OPENVINO_LIB_DIR})
```

---

### 坑 7：运行时找不到 `.so` 文件

**现象**：编译成功，但运行时提示 `error while loading shared libraries: libopenvino.so.2621: cannot open shared object file`。

**原因**：Linux 动态链接器只在系统标准路径（`/usr/lib`、`/lib` 等）和 `LD_LIBRARY_PATH` 中搜索。

**解决**：
```bash
# 临时
export LD_LIBRARY_PATH=$HOME/.local/lib/python3.10/site-packages/openvino/libs:$LD_LIBRARY_PATH

# 永久
echo 'export LD_LIBRARY_PATH=$HOME/.local/lib/python3.10/site-packages/openvino/libs:$LD_LIBRARY_PATH' >> ~/.bashrc
```

---

### 坑 8：模型文件路径问题

**现象**：
```
Could not open the file: "./best.xml"
```

**原因**：模型文件没有放在 `build` 目录下，或在子目录中但路径不对。

**解决**：
```bash
# 直接放在 build 目录
cp best.xml best.bin ~/mvs_openvino_demo/build/
./mvs_openvino_demo ./best.xml

# 或放在子目录
mkdir -p ~/mvs_openvino_demo/build/best_openvino_model
cp best.xml best.bin ~/mvs_openvino_demo/build/best_openvino_model/
./mvs_openvino_demo ./best_openvino_model/best.xml
```

---

### 坑 9：源文件移动到 `src/` 目录后编译报错

**现象**：
```
cc1plus: fatal error: /home/hu/mvs_openvino_demo/main.cpp: 没有那个文件或目录
```

**原因**：`CMakeLists.txt` 中 `add_executable` 路径仍是 `main.cpp`，未更新。

**解决**：
```cmake
# ❌ 旧
add_executable(${PROJECT_NAME} main.cpp)

# ✅ 新
add_executable(${PROJECT_NAME} src/main.cpp)
```

---

## 常见问题

### 编译阶段

| 问题 | 解决方案 |
|------|---------|
| `找不到 openvino.hpp` | `pip install openvino`，确认 `~/.local/lib/python3.10/site-packages/openvino/include/` 下有此文件 |
| `找不到 -lopenvino` | 检查 `build/openvino_links/` 下是否生成了符号链接，重新 `cmake ..` |
| OpenCV 找不到 | `sudo apt install libopencv-dev` |
| 迈德威视 SDK 找不到 | 修改 `CMakeLists.txt` 中 `MV_SDK_ROOT` 为实际 SDK 路径 |
| `main.cpp` 找不到 | 确认 `CMakeLists.txt` 中路径与源文件实际位置一致（当前为 `src/main.cpp`） |

### 运行阶段

| 问题 | 解决方案 |
|------|---------|
| 模型文件打不开 | 确保 `.xml` 和 `.bin` 在同一目录，路径正确 |
| `.so` 找不到 | 执行 `export LD_LIBRARY_PATH=...` 或写入 `~/.bashrc` |
| 检测不到相机 | `lsusb` 检查设备；可能需要 `sudo` 或 udev 规则 |
| 推理速度慢 | 将 `"AUTO"` 改为 `"GPU"`（需 Intel GPU）；降低相机分辨率 |
| 画面卡死 | 检查是否忘记调用 `CameraReleaseImageBuffer` |
| 检测框不准 | 调整 `CONF_THRESHOLD`（默认 0.4）和 `NMS_THRESHOLD`（默认 0.5） |

### 架构相关

| 问题 | 解决方案 |
|------|---------|
| `python3.10` 路径不存在 | 可能是 `python3.11` 或 `python3.12`，`find ~/.local -name "openvino.hpp"` 查找实际路径 |
| 32 位系统 | CMakeLists.txt 已自动处理，SDK 会选 `lib/x86` |
| 多台相机 | 修改 `iCameraCounts` 并使用数组存储多个 `tSdkCameraDevInfo` |
