# Deployment

本文档面向实际部署人员，描述当前工程在 NUC + Ubuntu 环境下的部署目标、依赖准备、构建流程和部署检查要求。

## 1. 部署目标

当前目标平台：

- NUC
- Ubuntu

部署目的：

- 运行主视觉程序 `bean_vision_framework`
- 运行工业相机预览程序 `camera_preview_demo`
- 运行串口联调程序 `serial_debug_demo`

说明：

- 主程序用于完整视觉主流程
- `camera_preview_demo` 用于工业相机预览和 ROI 校验
- `serial_debug_demo` 用于独立串口联调

## 2. NUC 环境准备

推荐系统：

- Ubuntu 20.04 或 22.04

建议先安装基础工具：

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  git \
  cmake \
  libopencv-dev \
  libyaml-cpp-dev
```

环境要求说明：

- `gcc/g++`
  - 用于 C++17 工程编译
- `git`
  - 用于获取和更新代码
- `cmake`
  - 用于生成和构建工程
- OpenCV
  - 用于图像处理、显示和绘图
- `yaml-cpp`
  - 用于读取 YAML 配置

建议确认：

```bash
g++ --version
cmake --version
pkg-config --modversion opencv4
```

## 3. 第三方库部署

### ONNX Runtime

作用：

- 为主程序提供 ONNX 模型推理能力

当前默认位置：

- `bisai/onnxruntime-linux-x64-1.27.0`

说明：

- 当前工程的 `CMakeLists.txt` 默认从仓库内该路径查找 ONNX Runtime
- 构建后会将 `libonnxruntime.so*` 复制到输出目录

如果 ONNX Runtime 不在默认位置，可在 CMake 时显式传入：

```bash
-DONNXRUNTIME_ROOT=/path/to/onnxruntime
```

环境变量：

- 当前方案不依赖额外环境变量作为主方式
- 优先通过 CMake 参数或仓库内固定路径解决

部署前建议检查：

```bash
ls bisai/onnxruntime-linux-x64-1.27.0/include
ls bisai/onnxruntime-linux-x64-1.27.0/lib
```

### MindVision SDK

作用：

- 为工业相机输入和 `camera_preview_demo` 提供相机能力

SDK 根目录至少应包含：

- `include/CameraApi.h`
- `lib/x64/libMVSDK.so`

当前 CMake 变量：

```bash
-DMINDVISION_ROOT=/path/to/linuxSDK_V2.x.x
```

如果未手动指定，当前工程会尝试自动搜索若干常见位置，包括：

- `./camera/linuxSDK_V2.1.0.49202602041120`
- `./camera`
- `../linuxSDK_V2.1.0.49202602041120`
- `../camera/linuxSDK_V2.1.0.49202602041120`
- `./third_party/camera/linuxSDK_V2.1.0.49202602041120`
- `./third_party/camera`
- `./sdk/linuxSDK_V2.1.0.49202602041120`
- `./sdk`

说明：

- 如果 SDK 不在自动搜索路径，部署时必须显式指定 `MINDVISION_ROOT`
- 构建后会将 `libMVSDK.so` 复制到相机相关目标输出目录

部署前建议检查：

```bash
ls /你的SDK目录/include/CameraApi.h
ls /你的SDK目录/lib/x64/libMVSDK.so
```

相机运行要求：

- 使用 MindVision Linux SDK
- 工业相机相关目标需要在构建阶段找到 SDK

## 4. 工程部署流程

示例流程：

```bash
git clone <repo-url>
cd bean_vision_framework
git checkout feature_real_robot_pipeline
mkdir -p build
cd build
cmake ..
cmake --build . -j$(nproc)
```

如果 ONNX Runtime 或 MindVision SDK 不在默认位置：

```bash
git clone <repo-url>
cd bean_vision_framework
git checkout feature_real_robot_pipeline
mkdir -p build
cd build
cmake .. \
  -DONNXRUNTIME_ROOT=/path/to/onnxruntime \
  -DMINDVISION_ROOT=/path/to/linuxSDK_V2.x.x
cmake --build . -j$(nproc)
```

分支说明：

- `main`：主分支
- `lzy`：稳定基线分支
- `feature_real_robot_pipeline`：当前比赛主流程开发分支

建议：

- 如果是比赛功能开发或联调，优先确认部署分支是否与当前开发任务一致
- 不要直接假设所有机器都已有正确的 SDK 路径

## 5. 运行目录要求

建议从构建输出目录运行程序，例如：

```bash
cd build
./bean_vision_framework ../config/debug_command_image.yaml
```

运行目录需要满足以下约束：

### 模型文件

- 配置中的模型路径必须有效
- 模型文件通常位于仓库内 `bisai/` 相关目录
- 若使用 `best.onnx`，同目录下不能缺少对应数据文件

### 配置文件

- 主程序和 demo 通过 YAML 文件启动
- 建议保持 `config/` 目录与工程源码一起部署

### 动态库

- `libonnxruntime.so*` 应存在于目标输出目录，或可被 RPATH 正确找到
- `libMVSDK.so` 应存在于相机相关输出目录，或可被运行时正确找到

建议检查：

```bash
ldd ./bean_vision_framework | grep "not found"
ldd ./camera_preview_demo | grep "not found"
```

### 日志与调试输出

- 当前工程会产生调试输出目录和图片保存结果
- 建议部署时保留可写工作目录
- 如果现场不需要图形窗口，可通过配置关闭显示，仅保留日志和输出图

## 6. 工业相机部署注意事项

部署要求总结：

- 优先使用 USB 3.0 直连
- 不建议使用 USB 2.0 扩展坞
- MindVision SDK 需要正确安装并在构建阶段可被找到
- 需要确保当前用户具备访问相机相关设备的权限
- 如现场依赖 udev 规则，应在部署前完成安装

补充说明：

- `lsusb` 能看到设备，不等于 SDK 一定能正常打开相机
- 如果部署在虚拟机环境，USB 透传稳定性不能默认等同于实体 NUC
- 相机部署阶段只检查“设备、SDK、权限、链路”是否成立，具体错误排查放到 `DEBUG_GUIDE.md`

## 7. 部署检查清单

建议按以下顺序检查：

- [ ] Ubuntu 版本符合要求
- [ ] `g++` / `cmake` / `git` 已安装
- [ ] OpenCV 和 `yaml-cpp` 已安装
- [ ] ONNX Runtime 路径有效
- [ ] MindVision SDK 路径有效
- [ ] `cmake ..` 成功
- [ ] `cmake --build . -j$(nproc)` 成功
- [ ] `bean_vision_framework --help` 可执行
- [ ] `camera_preview_demo --help` 可执行
- [ ] `serial_debug_demo --help` 可执行
- [ ] ONNX 模型可被正确加载
- [ ] 工业相机可被 SDK 识别
- [ ] 串口设备可被系统识别
- [ ] 运行目录中的动态库无缺失
