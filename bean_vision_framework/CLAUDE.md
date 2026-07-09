# CLAUDE.md

本文件为 Claude Code（claude.ai/code）在此仓库中工作时提供指引。

## 编译命令

### Linux（本机）

```bash
cd bean_vision_framework
mkdir -p build && cd build
cmake ..
cmake --build . -j$(nproc)
```

Linux 专用测试配置位于 `config/app_linux_test.yaml`（使用 `bisai/onnxruntime-linux-x64-1.27.0/` 下的 ONNX Runtime）。

### 运行

```bash
# 在 build 目录下执行：
./bean_vision_framework ../config/app.yaml
# 或使用一键 demo 脚本：
bash scripts/run_demo.sh
```

串口调试工具是第二个构建目标：`./serial_debug_demo`。

## 架构总览

整个流水线是一条严格分层的同步调用链——没有多线程、没有异步、没有回调：

```
InputManager → BeanNumberDetector → RoiParser → TaskStateMachine（内部使用 VisionMemory 做跨帧缓存）→ TaskGenerator → Protocol → SerialPort
```

**核心边界规则：** 每个模块只做一件事。跨边界混入其他职责是这个代码库里最常见的 bug 类型。

| 模块 | 负责 | 禁止做的事情 |
|---|---|---|
| `InputManager` | 获取 `cv::Mat` 图像帧（mock/image/video/MindVision 相机） | 检测、ROI 逻辑 |
| `BeanNumberDetector` | YOLO/ONNX 推理 → `vector<Detection>` | ROI 分配、任务生成、串口 |
| `RoiParser` | 将检测框中心点映射到 P1-P3 / L4-L8 固定位置 → `VisionResult` | 多帧缓存、任务生成 |
| `VisionMemory` | 跨帧位置缓存（先扫豆子区，后扫数字区） | 检测、串口 |
| `TaskStateMachine` | 状态流转：WAIT_BEAN → SCAN_BEANS → SEND_BEAN_BIND → WAIT_DIGIT → SCAN_DIGITS → GENERATE_FINAL → SEND_FINAL → DONE | 检测、协议打包 |
| `TaskGenerator` | 豆子→数字映射：黄豆→digit_1，绿豆→digit_2，白芸豆→digit_3 | 协议打包、串口 |
| `Protocol` | 将 `TaskResult`/`VisionResult` 打包成带帧头的字节包（`0xA5 cmd len seq payload crc_l crc_h`，CRC16-MODBUS） | 串口开关、任务语义 |
| `SerialPort` | 发送字节（mock 模式打印十六进制，真实模式用 termios 串口） | 协议组帧、任务含义 |

### 两种运行模式

1. **命令驱动模式**（`command.source: terminal`）：程序等待用户在终端输入 `arrive_bean <图片路径>` / `arrive_digit <图片路径>` / `reset` / `quit`。每条命令对指定图片执行检测。这是当前主模式。

2. **帧循环模式**（`command.source: none`）：连续执行 `input.read()` → `detect()` → `process()` 循环，用于视频/摄像头调试。

### 检测后端

当前仅 `onnxruntime` 可用。`mock` 后端在命令驱动流程中已被显式禁用。ONNX 模型是 YOLO 导出格式，输出 `1×C×N` 张量（每个候选框 4 个坐标 + 各类别分数）。后处理流程：letterbox 去补边 → 置信度阈值过滤 → NMS。

## 配置系统

`src/core/AppConfig.cpp` 包含一个手写的类 YAML 解析器（不依赖 yaml-cpp）。相对路径解析顺序为：当前工作目录 → 配置文件所在目录 → 项目根目录（配置目录的父目录）。这意味着配置文件中的路径是相对于配置文件位置的，而非相对于二进制的工作目录。

配置加载链：`app.yaml` → `classes.yaml`（类别名 + 别名，如 `data_1→digit_1`）→ `roi.yaml`（P1-P3 取货区 + L4-L8 放置区矩形）→ `serial.yaml`（mock/enable/port/baudrate）。

新增配置字段时，需要同时修改三处：`include/core/AppConfig.h` 中的结构体、`AppConfig::load()` 中的解析 switch、以及对应的 YAML 文件。

## 平台条件编译

- `BVP_WITH_MINDVISION` — Linux 下启用 MindVision 工业相机 SDK 时定义。所有 MindVision 相关代码都包裹在 `#ifdef BVP_WITH_MINDVISION` 内。
- ONNX Runtime 路径固定使用 `bisai/onnxruntime-linux-x64-1.27.0/`。

## 常见修改对应的关键文件

- **改 ROI 区域**：只改 `config/roi.yaml`，无需动代码
- **换 YOLO 模型**：改 `config/app.yaml` 中的 `detector.model_path`
- **改豆子→数字映射规则**：改 `src/task/TaskGenerator.cpp`
- **改串口协议帧格式**：改 `src/communication/Protocol.cpp`
- **切换到真实串口**：在 `config/serial.yaml` 中设置 `mock: false` 和 `port: /dev/ttyACM0`
- **新增命令**：改 `src/task/TaskStateMachine.cpp` 中的 `processCommand()`

## 依赖

- C++17、CMake ≥3.16、OpenCV（4.x）
- ONNX Runtime（Linux：`bisai/onnxruntime-linux-x64-1.27.0/`）
- MindVision Linux SDK（由 `WITH_MINDVISION` 和 `MINDVISION_ROOT` 控制）
