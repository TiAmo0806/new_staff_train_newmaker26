# Architecture

本文档描述当前工程已经实现的真实架构，只覆盖当前代码中已经存在并可运行的模块与数据流，不把未来规划写成现状。

## 1. 工程整体架构

当前工程可以按职责分为五层：

- 输入层
- 相机层
- 推理层
- 视觉逻辑层
- 通信层

整体文字流程图：

```text
命令来源
  -> TerminalCommandSource / SerialCommandSource

图像来源
  -> InputManager
  -> CameraManager（仅 industrial camera 路径）
  -> MindVision SDK

图像帧
  -> BeanNumberDetector
  -> RoiParser
  -> VisionMemory / TaskStateMachine
  -> TaskGenerator
  -> Protocol
  -> SerialPort
```

如果只看主视觉链路，可简化为：

```text
InputManager
  -> BeanNumberDetector
  -> RoiParser
  -> VisionMemory / TaskStateMachine
  -> TaskGenerator
  -> Protocol
  -> SerialPort
```

### 分层说明

#### 输入层

负责统一产出 `cv::Mat`，不处理检测、ROI、任务和协议。

当前模块：

- `InputManager`
- `TerminalCommandSource`
- `SerialCommandSource`

#### 相机层

负责工业相机生命周期、相机参数应用和图像标准化输出。

当前模块：

- `CameraManager`

#### 推理层

负责模型加载和检测框输出。

当前模块：

- `BeanNumberDetector`

#### 视觉逻辑层

负责把检测结果变成固定位置语义，再变成任务语义。

当前模块：

- `RoiParser`
- `VisionMemory`
- `TaskStateMachine`
- `TaskGenerator`
- `MultiFrameRecognizer`

#### 通信层

负责协议帧定义、串口收发、ACK 和重发。

当前模块：

- `Protocol`
- `SerialPort`
- `CRC16`
- `ByteConverter`

## 2. 主程序调用流程

主程序入口在 [src/main.cpp](/home/ygk/yolo_competition/bean_vision_framework/src/main.cpp:1)。

当前启动流程如下：

```text
main.cpp
  -> AppConfig::load()
  -> InputManager
  -> BeanNumberDetector
  -> RoiParser
  -> TaskGenerator
  -> TaskStateMachine
  -> Protocol
  -> SerialPort
  -> CommandSource（terminal / serial / none）
```

### 启动阶段

`main.cpp` 当前会完成这些初始化动作：

1. 加载 `AppConfig`
2. 根据配置构造 `InputManager`
3. 构造 `BeanNumberDetector`
4. 构造 `RoiParser`
5. 构造 `TaskGenerator`
6. 构造 `TaskStateMachine`
7. 构造 `Protocol`
8. 构造 `SerialPort`
9. 调用 `detector.loadModel()`
10. 调用 `serial.open()`
11. 根据 `command.source` 选择：
   - `TerminalCommandSource`
   - `SerialCommandSource`
   - 或进入无命令的连续帧流程

### 两条主运行路径

#### 命令驱动路径

适用于：

- `debug_command_image`
- `debug_camera_mock`
- `debug_image_real_serial`
- `real_robot`

调用链：

```text
CommandSource::next()
  -> TaskStateMachine::processCommand() / processCameraCommand()
  -> Detector
  -> RoiParser
  -> VisionMemory
  -> TaskGenerator
  -> Protocol
  -> SerialPort
```

#### 连续帧路径

适用于 `command.source = none` 的场景。

调用链：

```text
InputManager::open()
  -> InputManager::read()
  -> BeanNumberDetector::detect()
  -> RoiParser::parse()
  -> TaskStateMachine::process()
  -> TaskGenerator
  -> Protocol
  -> SerialPort
```

## 3. 模块职责

### CameraManager

头文件：

- [include/camera/CameraManager.h](/home/ygk/yolo_competition/bean_vision_framework/include/camera/CameraManager.h:1)

实现：

- [src/camera/CameraManager.cpp](/home/ygk/yolo_competition/bean_vision_framework/src/camera/CameraManager.cpp:1)

为什么存在：

- 工业相机生命周期与普通输入源管理职责不同
- MindVision SDK 不应继续直接耦合在 `InputManager` 内部
- 相机参数配置化之后，需要一个独立边界来承接 SDK、参数应用和图像标准化输出

当前职责：

- MindVision SDK 初始化
- 枚举和打开工业相机
- 应用相机参数配置
- 读取一帧 `cv::Mat`
- 在相机层完成固定图像方向处理
- 关闭相机并释放 SDK 资源

与 `InputManager` 的关系：

- `InputManager` 在 `mindvision_camera` 路径下持有 `CameraManager`
- `InputManager` 不再直接包含或调用 MindVision SDK

MindVision SDK 边界：

- SDK 类型和 `CameraApi.h` 只存在于 `CameraManager.cpp`
- 上层模块只看到 `cv::Mat`

### InputManager

头文件：

- [include/input/InputManager.h](/home/ygk/yolo_competition/bean_vision_framework/include/input/InputManager.h:1)

实现：

- [src/input/InputManager.cpp](/home/ygk/yolo_competition/bean_vision_framework/src/input/InputManager.cpp:1)

当前支持输入类型：

- `mock`
- `image`
- `video`
- `camera`
- `mindvision_camera`

职责范围：

- 根据 `input.type` 选择输入来源
- 打开输入源
- 向上层统一输出 `cv::Mat`
- 在适当时机释放输入资源

不负责：

- MindVision SDK 细节
- 检测
- ROI 解析
- 任务生成
- 串口协议

### Detector

当前模块名：

- `BeanNumberDetector`

头文件：

- [include/detector/BeanNumberDetector.h](/home/ygk/yolo_competition/bean_vision_framework/include/detector/BeanNumberDetector.h:1)

实现：

- [src/detector/BeanNumberDetector.cpp](/home/ygk/yolo_competition/bean_vision_framework/src/detector/BeanNumberDetector.cpp:1)

当前职责：

- 加载 ONNX Runtime 模型
- 对输入图像执行 YOLO 推理
- 完成模型输入预处理和输出后处理
- 输出 `std::vector<Detection>`

输出边界：

- `Detector` 只输出检测框
- 不判断目标属于哪个 ROI
- 不生成搬运任务

### RoiParser

头文件：

- [include/parser/RoiParser.h](/home/ygk/yolo_competition/bean_vision_framework/include/parser/RoiParser.h:1)

实现：

- [src/parser/RoiParser.cpp](/home/ygk/yolo_competition/bean_vision_framework/src/parser/RoiParser.cpp:1)

当前职责：

- 将检测框中心点映射到固定 ROI
- 区分豆子区 `P1/P2/P3` 与数字区 `L4-L8`
- 同一 ROI 内选择置信度最高的候选
- 输出 `VisionResult`

当前规则：

- 豆子类只允许进入 `P1/P2/P3`
- 数字类只允许进入 `L4-L8`

### TaskGenerator

头文件：

- [include/task/TaskGenerator.h](/home/ygk/yolo_competition/bean_vision_framework/include/task/TaskGenerator.h:1)

实现：

- [src/task/TaskGenerator.cpp](/home/ygk/yolo_competition/bean_vision_framework/src/task/TaskGenerator.cpp:1)

当前职责：

- 根据 `VisionResult` 生成搬运任务列表

当前已实现规则：

```text
soybean           -> digit_1
mung_bean         -> digit_2
white_kidney_bean -> digit_3
```

### StateMachine

头文件：

- [include/task/TaskStateMachine.h](/home/ygk/yolo_competition/bean_vision_framework/include/task/TaskStateMachine.h:1)

实现：

- [src/task/TaskStateMachine.cpp](/home/ygk/yolo_competition/bean_vision_framework/src/task/TaskStateMachine.cpp:1)

当前职责：

- 管理豆子阶段和数字阶段的状态流转
- 协调 `VisionMemory`、`TaskGenerator`、`Protocol` 和 `SerialPort`
- 响应终端命令或串口命令

当前状态：

```text
WAIT_BEAN_COMMAND
-> SCAN_BEANS
-> SEND_BEAN_BIND
-> WAIT_DIGIT_COMMAND
-> SCAN_DIGITS
-> GENERATE_FINAL_TASK
-> SEND_FINAL_TASK
-> DONE
```

说明：

- 当前已实现的是两阶段主流程
- 不是多视角数字状态机
- 不是云台协同状态机

### SerialPort

头文件：

- [include/communication/SerialPort.h](/home/ygk/yolo_competition/bean_vision_framework/include/communication/SerialPort.h:1)

实现：

- [src/communication/SerialPort.cpp](/home/ygk/yolo_competition/bean_vision_framework/src/communication/SerialPort.cpp:1)

当前职责：

- 打开真实串口或进入 mock 模式
- 发送完整协议帧
- 读取串口可用数据
- 等待 ACK
- 超时重发
- 输出协议日志

当前定位：

- 它只负责字节流和端口
- 业务语义仍由 `Protocol` 和 `TaskStateMachine` 处理

### Protocol

头文件：

- [include/communication/Protocol.h](/home/ygk/yolo_competition/bean_vision_framework/include/communication/Protocol.h:1)

实现：

- [src/communication/Protocol.cpp](/home/ygk/yolo_competition/bean_vision_framework/src/communication/Protocol.cpp:1)

当前职责：

- 将视觉结果和任务结果编码成协议帧
- 解析串口收到的协议帧
- 定义命令字、payload 结构和协议包格式

与 `SerialPort` 的边界：

- `Protocol` 负责“包长什么样”
- `SerialPort` 负责“包怎么发和怎么收”

## 4. 当前视觉数据流

当前主数据流如下：

```text
图像
  -> BeanNumberDetector
  -> Detection 列表
  -> RoiParser
  -> VisionResult
  -> VisionMemory / TaskStateMachine
  -> TaskGenerator
  -> TaskResult / BeanBind
  -> Protocol
  -> SerialPort
```

### 阶段 1：豆子识别

```text
图像
  -> 检测豆子
  -> ROI 解析到 P1/P2/P3
  -> VisionMemory 缓存豆子结果
  -> 生成 BeanBind
  -> Protocol::makeBeanBindPacket()
  -> SerialPort::write()
```

### 阶段 2：数字识别

```text
图像
  -> 检测数字
  -> ROI 解析到 L4-L8
  -> VisionMemory 缓存数字结果
  -> mergedResult()
  -> TaskGenerator::generate()
  -> Protocol::makeTaskPacket()
  -> SerialPort::write()
```

### 多帧识别路径

在 `debug_camera_mock` 和 `real_robot` 这类相机驱动模式下，还会经过：

```text
InputManager
  -> MultiFrameRecognizer
  -> 多帧 detect + parse
  -> 投票融合
  -> VisionResult
```

`MultiFrameRecognizer` 是视觉逻辑层的一部分，用于提高相机模式下的稳定性，不改变主数据流的最终出口。

## 5. 配置系统

当前配置入口：

- [include/core/AppConfig.h](/home/ygk/yolo_competition/bean_vision_framework/include/core/AppConfig.h:1)
- [src/core/AppConfig.cpp](/home/ygk/yolo_competition/bean_vision_framework/src/core/AppConfig.cpp:1)

`AppConfig` 是当前工程的统一配置聚合层，`main.cpp` 只加载一次，再将子配置分发给各模块。

当前子配置包括：

- `runtime`
  - 运行模式
- `input`
  - 输入来源类型与路径
- `camera`
  - 相机参数配置
- `command`
  - 命令来源
- `scan`
  - 多帧识别策略
- `detector`
  - 模型、阈值、类别映射
- `roi`
  - 固定 ROI
- `serial`
  - 串口与 ACK 策略
- `debug`
  - 调试开关和输出目录

配置分发关系：

```text
AppConfig
  -> InputManager(input, camera)
  -> BeanNumberDetector(detector)
  -> RoiParser(roi)
  -> MultiFrameRecognizer(scan/debug/roi/detector 等)
  -> SerialPort(serial)
```

说明：

- `camera` 已经是独立配置结构，不再散落在输入层实现中
- `roi`、`classes`、`serial` 等仍通过 `AppConfig::load()` 继续装配

## 6. 当前工程目录说明

### `src/`

保存主程序和所有核心实现。

当前子模块包括：

- `src/camera/`
- `src/command/`
- `src/communication/`
- `src/core/`
- `src/detector/`
- `src/input/`
- `src/parser/`
- `src/recognition/`
- `src/task/`
- `src/utils/`

### `include/`

保存与 `src/` 对应的头文件接口。

当前结构与源码分层基本一一对应，便于从模块边界理解工程结构。

### `config/`

保存 YAML 配置：

- 运行模式配置
- ROI 配置
- 串口配置
- 类别映射
- 相机与调试参数

### `tools/`

保存独立 demo：

- `tools/camera_preview_demo`
  - 工业相机预览与 ROI 校验
- `tools/serial_debug_demo`
  - 串口协议联调 demo

### `models`

当前仓库没有单独命名为 `models/` 的目录。

当前模型和运行时相关资源主要位于：

- `bisai/bean_digit_v1_verified_20260624/`
- `bisai/onnxruntime-linux-x64-1.27.0/`

因此当前工程的“模型目录”更准确地说是 `bisai/` 下的模型与运行库资源。

## 7. 当前未实现部分

以下内容不是当前架构的一部分，不应写成“已实现”：

- 多视角管理
- 云台协同调度
- 自动 pitch / yaw 规划
- 视觉主动控制云台
- 多视角 ROI 集管理
- 数字阶段的中心/左/右视角状态机

当前代码事实是：

- 状态机仍是“两阶段识别 + 任务发送”
- ROI 仍是固定坐标集合
- 串口协议已支持当前豆子/数字阶段联调，但未扩展成完整云台协同协议
