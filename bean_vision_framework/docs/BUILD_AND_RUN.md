# Build And Run

本文档描述当前有效的工程环境要求、目录结构、依赖说明、CMake 构建流程和可执行程序入口。

## 1. 环境要求

推荐环境：

- Ubuntu 20.04 或 22.04
- `gcc/g++`，支持 C++17
- `cmake` 3.16 及以上
- OpenCV
- `yaml-cpp`
- ONNX Runtime
- MindVision Linux SDK

基础构建工具建议安装：

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libopencv-dev libyaml-cpp-dev
```

说明：

- OpenCV 用于图像采集、显示、预处理和绘制。
- `yaml-cpp` 用于读取运行配置。
- ONNX Runtime 用于 YOLO 模型推理。
- MindVision SDK 仅在工业相机相关目标中使用。

## 2. 获取代码

示例：

```bash
git clone <repo-url>
cd bean_vision_framework
```

分支约定：

- `main`：主分支
- `lzy`：稳定基线分支
- `feature_real_robot_pipeline`：比赛主流程开发分支

如果是继续当前工程开发，优先确认自己所在分支是否符合当前任务目标。

## 3. 工程目录结构

当前主目录说明：

- `src/`
  - 主程序与核心实现
- `include/`
  - 头文件
- `config/`
  - YAML 配置文件
- `tools/`
  - 独立 demo
- `docs/`
  - 工程文档
- `bisai/`
  - 当前仓库内使用的模型与 ONNX Runtime 运行库

说明：

- 当前仓库中没有统一的 `thirdpart/` 目录。
- 第三方依赖一部分来自系统安装，一部分来自仓库内的 `bisai/`，另一部分来自本机安装的 MindVision SDK。

## 4. 第三方依赖

### ONNX Runtime

作用：

- 为主程序提供 ONNX 模型推理能力

当前 CMake 默认路径：

- `bisai/onnxruntime-linux-x64-1.27.0`

如果默认路径不适用，可显式传入：

```bash
-DONNXRUNTIME_ROOT=/path/to/onnxruntime
```

### MindVision SDK

作用：

- 为 `mindvision_camera` 输入模式和 `camera_preview_demo` 提供工业相机能力

当前 CMake 行为：

- 默认开启 `WITH_MINDVISION=ON`
- 若未手动指定 `MINDVISION_ROOT`，会自动尝试以下常见位置：
  - `./camera/linuxSDK_V2.1.0.49202602041120`
  - `./camera`
  - `../linuxSDK_V2.1.0.49202602041120`
  - `../camera/linuxSDK_V2.1.0.49202602041120`
  - `./third_party/camera/linuxSDK_V2.1.0.49202602041120`
  - `./third_party/camera`
  - `./sdk/linuxSDK_V2.1.0.49202602041120`
  - `./sdk`

如果自动搜索不到，需要显式传入：

```bash
-DMINDVISION_ROOT=/path/to/linuxSDK_V2.x.x
```

SDK 根目录至少应包含：

- `include/CameraApi.h`
- `lib/x64/libMVSDK.so`

## 5. CMake 编译流程

推荐从项目根目录执行：

```bash
mkdir -p build
cd build
cmake ..
cmake --build . -j$(nproc)
```

如果 ONNX Runtime 或 MindVision SDK 不在默认位置，可显式传参：

```bash
mkdir -p build
cd build
cmake .. \
  -DONNXRUNTIME_ROOT=/path/to/onnxruntime \
  -DMINDVISION_ROOT=/path/to/linuxSDK_V2.x.x
cmake --build . -j$(nproc)
```

补充说明：

- 主程序会链接 ONNX Runtime。
- 工业相机相关目标在检测到 MindVision SDK 时会自动开启相机支持。
- 构建后会将 `libonnxruntime.so*` 复制到目标输出目录。
- 构建后会将 `libMVSDK.so` 复制到相机相关目标输出目录。

## 6. 可执行程序说明

当前主要目标：

### `bean_vision_framework`

作用：

- 主程序
- 负责完整视觉主流程
- 支持图片、视频、普通相机、MindVision 工业相机、串口命令等运行方式

示例：

```bash
./bean_vision_framework ../config/debug_command_image.yaml
./bean_vision_framework --help
```

### `camera_preview_demo`

作用：

- 独立验证 MindVision 工业相机采集
- 预览画面
- 叠加 ROI
- 保存当前帧

示例：

```bash
./camera_preview_demo ../config/camera_preview_demo.yaml
./camera_preview_demo --help
```

### `serial_debug_demo`

作用：

- 独立验证串口收发与协议调试

说明：

- 当前工程同时生成 `serial_protocol_demo` 和 `serial_debug_demo`
- 两者共用同一套源码，主要用于独立串口联调

示例：

```bash
./serial_debug_demo --help
./serial_debug_demo --mock
```

## 7. 配置文件使用

`config/` 目录用于保存程序运行配置。

当前常见配置包括：

- 主程序模式配置
- ROI 配置
- 串口配置
- 相机配置
- 类别映射配置

使用方式：

- 主程序和 demo 通过传入 YAML 文件路径决定运行模式
- 业务参数、设备参数和调试参数统一从配置文件读取

本文档不展开具体配置字段。后续统一配置参考文档将收敛到 `CONFIG_REFERENCE.md`。

## 8. 常见问题入口

本页不展开大量排障内容。

排障入口：

- 规划中的 `DEBUG_GUIDE.md`
- 当前阶段可先参考旧文档 [调试与排障指南.md](./debug/调试与排障指南.md)

如果是部署到 NUC，建议同时参考旧文档：

- [NUC部署指南.md](./deployment/NUC部署指南.md)
