# Build Test Report

## 环境信息

- Ubuntu 版本：
  - `Ubuntu Core 24`
- cmake 版本：
  - 不存在，`cmake --version` 返回 `cmake: command not found`
- gcc 版本：
  - 不存在，`g++ --version` 返回 `g++: command not found`
- make 版本：
  - 不存在，`make --version` 返回 `make: command not found`

补充说明：

- 当前环境中也不存在 `apt` / `apt-get`
- `snap` 虽然路径存在，但在当前运行环境中不可执行，返回 `Permission denied`
- 因此无法在本次会话内补装构建工具链

## 工程入口

- Git 根目录：
  - `/home/ygk/yolo_competition`
- 当前仓库可见目录：
  - `bean_vision_framework`
  - `core`
  - `linuxSDK_V2.1.0.49202602041120`
  - `third_party`
- CMakeLists 位置：
  - `./bean_vision_framework/CMakeLists.txt`
  - `./bean_vision_framework/bisai/CMakeLists.txt`
  - `./core/bean_vision_framework/CMakeLists.txt`
  - `./core/bean_vision_framework/bisai/CMakeLists.txt`

结论：

- 仓库根目录没有顶层 `CMakeLists.txt`
- 主工程入口应为：
  - `bean_vision_framework`

## 配置结果

按要求准备执行：

```bash
cd bean_vision_framework
cmake -S . -B build
```

但在实际进入该步骤前，环境检查已经确认：

```text
/bin/bash: line 1: cmake: command not found
```

因此本次无法实际执行 `cmake -S . -B build`

## 编译结果

按流程应执行：

```bash
cmake --build build -j
```

但由于 `cmake` 本身不存在，配置阶段未能开始，因此编译阶段未执行。

结果：

- 未执行
- 无法获得有效编译输出

## 依赖分析

基于 `bean_vision_framework/CMakeLists.txt` 的静态分析：

- OpenCV
  - 使用 `find_package(OpenCV REQUIRED)`
  - 说明依赖系统已安装的 OpenCV 开发环境和 CMake 包配置
  - 当前未能验证是否已安装，因为 `cmake` 不可用

- ONNX Runtime
  - 主工程固定从以下路径查找：
    - `${CMAKE_SOURCE_DIR}/bisai/onnxruntime-linux-x64-1.27.0`
  - 也就是：
    - `bean_vision_framework/bisai/onnxruntime-linux-x64-1.27.0`
  - 该目录在本地磁盘上存在
  - 但它没有进入 Git 历史，属于本地第三方依赖恢复项

- MindVision
  - 通过 `MINDVISION_ROOT` 或多个候选路径查找 `CameraApi.h`
  - 找不到时只发出 warning，不一定导致主工程配置失败
  - 本地可见 SDK 目录：
    - `linuxSDK_V2.1.0.49202602041120`
    - `third_party/linuxSDK_V2.1.0.49202602041120`
  - 但是否能被当前主工程自动识别，仍需在有 `cmake` 的环境中实际验证

失败归类：

- 当前明确失败属于：
  - 环境问题
- 当前尚不能进一步确认是否还存在：
  - 第三方依赖路径问题
  - 工程配置问题

原因：

- 基础构建工具链不存在，导致验证流程在最前面就中断

## 是否影响Git清理

本次验证没有发现 Git 清理导致的直接问题。

明确说明：

- 当前失败是运行环境缺少 `cmake`、`g++`、`make`
- 不是因为 Git 历史清理损坏了源码、CMake 文件或配置
- 之前已经确认以下内容未进入 Git 历史：
  - `build`
  - `linuxSDK`
  - `onnxruntime`
  - `*.onnx`
  - `*.onnx.data`

因此：

- Git 清理结果本身没有被这次验证否定
- 这次无法完成构建，主因是环境不具备构建条件

## 后续建议

1. 在标准 Ubuntu 开发环境中进行验证，而不是当前 `Ubuntu Core` Snap 沙箱环境。
2. 先安装基础工具链：
   - `cmake`
   - `g++`
   - `make`
3. 然后在主工程目录执行：
   - `cd bean_vision_framework`
   - `cmake -S . -B build`
   - `cmake --build build -j`
4. 如果之后配置失败，再重点检查：
   - OpenCV 是否已安装并能被 `find_package(OpenCV)` 找到
   - `bean_vision_framework/bisai/onnxruntime-linux-x64-1.27.0` 是否完整存在
   - MindVision SDK 是否需要通过 `-DMINDVISION_ROOT=...` 明确指定
