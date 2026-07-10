# Build Verification Report

## 环境

- Git 根目录：`/home/ygk/yolo_competition`
- 当前分支：`lzy_clean`
- 当前构建命令：`cmake -S . -B build`
- 当前环境结果：系统中不存在 `cmake` 命令

## 工程结构检查

- Git 根目录：
  - `/home/ygk/yolo_competition`
- 主要 CMakeLists 位置：
  - `bean_vision_framework/CMakeLists.txt`
  - `bean_vision_framework/bisai/CMakeLists.txt`
- 主工程目录：
  - `bean_vision_framework/src`
  - `bean_vision_framework/include`
  - `bean_vision_framework/config`
  - `bean_vision_framework/docs`
- 第三方依赖位置：
  - MindVision SDK 本地目录：`linuxSDK_V2.1.0.49202602041120`
  - 额外副本目录：`third_party/linuxSDK_V2.1.0.49202602041120`
  - ONNX Runtime 本地目录：
    - `bean_vision_framework/bisai/onnxruntime-linux-x64-1.27.0`
    - `bean_vision_framework/bisai/onnxruntime-linux-x64-1.27 (1).0`

说明：

- 仓库根目录本身没有顶层 `CMakeLists.txt`
- 实际可见的主工程入口是 `bean_vision_framework/CMakeLists.txt`

## CMake配置分析

主工程 `bean_vision_framework/CMakeLists.txt` 的依赖查找逻辑如下：

- OpenCV
  - 通过 `find_package(OpenCV REQUIRED)` 查找
  - 依赖系统已安装的 CMake 包配置或标准查找路径

- ONNX Runtime
  - 通过固定路径变量：
    - `ONNXRUNTIME_ROOT="${CMAKE_SOURCE_DIR}/bisai/onnxruntime-linux-x64-1.27.0"`
  - 再通过：
    - `find_library(ONNXRUNTIME_LIB onnxruntime PATHS "${ONNXRUNTIME_ROOT}/lib" REQUIRED)`
  - 说明主工程默认从源码树内部的 `bisai/onnxruntime-linux-x64-1.27.0` 查找

- MindVision SDK
  - 通过缓存变量：
    - `MINDVISION_ROOT`
  - 若未手动指定，则依次尝试以下候选路径：
    - `${CMAKE_SOURCE_DIR}/camera/linuxSDK_V2.1.0.49202602041120`
    - `${CMAKE_SOURCE_DIR}/camera`
    - `${CMAKE_SOURCE_DIR}/../linuxSDK_V2.1.0.49202602041120`
    - `${CMAKE_SOURCE_DIR}/../camera/linuxSDK_V2.1.0.49202602041120`
    - `${CMAKE_SOURCE_DIR}/third_party/camera/linuxSDK_V2.1.0.49202602041120`
    - `${CMAKE_SOURCE_DIR}/third_party/camera`
    - `${CMAKE_SOURCE_DIR}/sdk/linuxSDK_V2.1.0.49202602041120`
    - `${CMAKE_SOURCE_DIR}/sdk`
  - 只有当 `${MINDVISION_ROOT}/include/CameraApi.h` 存在时才视为可用
  - 若找不到，CMake 只给 warning，不直接失败

- yaml-cpp
  - 在当前主工程 `bean_vision_framework/CMakeLists.txt` 中未发现 `find_package(yaml-cpp)` 或 `target_link_libraries(... yaml-cpp ...)`
  - 在 `bean_vision_framework/bisai/CMakeLists.txt` 中也未发现 `yaml-cpp`
  - 结论：当前检查到的 CMake 配置里没有显式声明 `yaml-cpp` 依赖

补充：

- `bean_vision_framework/bisai/CMakeLists.txt` 使用的是另一个局部工程入口
- 该子工程依赖：
  - `find_package(OpenCV REQUIRED COMPONENTS core videoio imgproc imgcodecs dnn)`
  - 固定路径 `onnxruntime-sdk`

## 编译结果

执行命令：

```bash
cmake -S . -B build
```

结果：

```text
/bin/bash: line 1: cmake: command not found
```

结论：

- 这次失败不是源码或 CMake 配置直接报错
- 失败原因是当前环境缺少 `cmake` 可执行文件
- 因此还没有进入真正的 CMake 配置与依赖解析阶段

## 缺失依赖

当前已确认缺失：

- `cmake` 工具本身

当前无法在本机命令结果中进一步确认但从 CMake 逻辑上推断可能需要：

- OpenCV 开发包
- ONNX Runtime 本地目录需位于主工程预期路径下
- MindVision SDK 如需工业相机功能，则需手动放置到 CMake 能识别的位置或通过 `-DMINDVISION_ROOT=...` 指定

说明：

- 由于 `cmake` 不存在，本次没有实际跑到 `find_package(OpenCV)` 或 `find_library(onnxruntime)` 阶段
- 所以 OpenCV / ONNX Runtime / MindVision 是否在当前机器可成功解析，不能仅凭这次命令最终确认

## 修复建议

- 先在当前开发环境安装 `cmake`
- 然后建议使用实际工程目录重新执行：
  - `cmake -S bean_vision_framework -B build`
- 如果之后 CMake 进入配置阶段再失败，优先检查：
  - OpenCV 是否已安装且可被 `find_package(OpenCV)` 找到
  - `bean_vision_framework/bisai/onnxruntime-linux-x64-1.27.0` 是否完整存在
  - 如需 MindVision，相机 SDK 是否通过 `-DMINDVISION_ROOT=/path/to/linuxSDK_V2.1.0.49202602041120` 明确指定

## 是否影响Git清理结果

不影响 Git 清理结果。

确认结果：

- `git ls-files` 中未发现以下内容被纳入仓库：
  - `build`
  - `linuxSDK`
  - `onnxruntime`
  - `*.onnx`
  - `*.onnx.data`

说明：

- 本次构建验证失败是环境工具链缺失
- 不是 Git 清理导致源码或仓库结构损坏
- Git 清理后的忽略目标仍未重新进入版本管理
