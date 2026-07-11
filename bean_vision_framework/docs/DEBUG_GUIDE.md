# Debug Guide

本文档记录当前 RoboMaster 视觉工程中已经实际遇到过、并且与本项目直接相关的调试经验与排查路径。  
它不是 Linux 通用教程，重点是帮助开发者快速判断问题落在哪一层。

## 1. 调试总体流程

建议按下面的顺序排查，不要一开始就怀疑 YOLO 或状态机：

```text
硬件
  -> 系统
  -> SDK / 动态库
  -> 程序启动
  -> 模型
  -> 业务流程
```

对应到当前工程，可以展开为：

```text
1. 硬件链路是否成立
   - NUC
   - 工业相机
   - USB 连接
   - 串口设备

2. 系统环境是否成立
   - Ubuntu
   - cmake / g++
   - OpenCV
   - yaml-cpp

3. 第三方库是否成立
   - MindVision SDK
   - ONNX Runtime

4. 程序能否启动
   - AppConfig
   - 输入源
   - 串口

5. 模型是否可用
   - best.onnx
   - best.onnx.data
   - ORT 动态库

6. 业务链路是否成立
   - ROI
   - 状态机
   - TaskGenerator
   - Protocol
   - SerialPort
```

推荐调试顺序：

1. `debug_command_image`
2. `camera_preview_demo`
3. `debug_camera_mock`
4. `serial_debug_demo`
5. `debug_image_real_serial`
6. `real_robot`

## 2. 编译问题

### 2.1 CMake 缓存导致路径不生效

当前工程的：

- `ONNXRUNTIME_ROOT`
- `MINDVISION_ROOT`

都定义为 CMake `CACHE PATH`。  
这意味着如果你之前配置过错误路径，后续即使移动了 SDK 或 ONNX Runtime，旧缓存也可能继续生效。

建议做法：

```bash
rm -rf build
mkdir build
cd build
cmake ..
cmake --build . -j$(nproc)
```

如果依赖不在默认位置：

```bash
cmake .. \
  -DONNXRUNTIME_ROOT=/path/to/onnxruntime \
  -DMINDVISION_ROOT=/path/to/linuxSDK_V2.x.x
cmake --build . -j$(nproc)
```

### 2.2 MindVision SDK 路径错误

常见现象：

- 编译时找不到 `CameraApi.h`
- 运行时相机相关目标没有工业相机支持

优先检查：

```bash
ls /你的SDK目录/include/CameraApi.h
ls /你的SDK目录/lib/x64/libMVSDK.so
```

说明：

- 如果 `MINDVISION_ROOT` 没配对，`camera_preview_demo` 和 `mindvision_camera` 路径就无法正常工作

### 2.3 ONNX Runtime 路径错误

常见现象：

- `cmake` 阶段找不到 `onnxruntime`
- 运行阶段缺少 `libonnxruntime.so`

优先检查：

```bash
ls bisai/onnxruntime-linux-x64-1.27.0/include
ls bisai/onnxruntime-linux-x64-1.27.0/lib
```

如果不在默认位置：

```bash
cmake .. -DONNXRUNTIME_ROOT=/path/to/onnxruntime
```

### 2.4 构建目录异常

当前项目开发过程中已经出现过“本地环境没有完整构建工具”导致无法完成验证的情况。  
如果你看到：

- `cmake: command not found`
- `g++: command not found`

先回到环境准备，不要继续怀疑源码。

## 3. 工业相机问题

### 3.1 先判断是不是相机链路问题

不要一开始就调 YOLO。先用：

```bash
./camera_preview_demo ../config/camera_preview_demo.yaml
```

如果这个 demo 都打不开相机，问题还在：

- 设备
- USB
- SDK
- 权限

而不在业务层。

### 3.2 `lsusb` 可见，不等于 SDK 一定可用

当前项目已经实际遇到过这种情况：

- `lsusb` 可以看到 MindVision 设备
- 但 SDK 初始化失败

所以排查相机时必须同时看：

```bash
lsusb
ldd ./camera_preview_demo | grep MVSDK
```

### 3.3 USB 2.0 扩展坞导致工业相机无法工作

这是当前项目已经验证过的真实经验。

现象：

- 设备能被 `lsusb` 识别
- 但 SDK 无法正常初始化
- 典型日志：
  - `user control fd open failed`

当前已验证的结论：

- USB 2.0 扩展坞不稳定，可能导致 MindVision 工业相机无法工作
- USB 3.0 直连接口可正常工作

当前建议：

- 工业相机优先使用 USB 3.0 直连
- 不要默认把“能枚举到 USB 设备”当成“相机可用”

### 3.4 VMware 环境只能做有限验证

当前项目排查过虚拟机环境下的相机问题，结论是：

- VMware 中 `lsusb` 能看到设备，不等于 SDK 能稳定打开相机
- 虚拟机 USB 透传不能等同于实体 NUC

建议：

- 真正的工业相机可用性，优先在实体 Ubuntu / NUC 上确认

### 3.5 MindVision SDK 运行日志的理解

当前排查中见过这类日志：

- `CtiPath: /usr/lib/fg_cxp.cti is error or system not find`
- `CXP system count:0`
- `user control fd open failed`

经验判断：

- `fg_cxp.cti` / `CXP system count:0`
  - 更像 SDK 启动时的扫描信息
  - 不是 USB 相机失败的唯一根因
- `user control fd open failed`
  - 更值得重点关注
  - 常与 USB 链路、SDK 运行环境或虚拟机透传有关

### 3.6 工业相机编译支持未启用

如果运行 `mindvision_camera` 模式时看到：

- `MindVision camera support is not enabled in this build.`

说明：

- 当前构建没有正确链接 MindVision SDK
- 先回到编译阶段检查 `MINDVISION_ROOT`

## 4. ONNX Runtime 问题

### 4.1 动态库缺失

常见现象：

- 程序能编译，但运行时报 `libonnxruntime.so` 找不到

检查：

```bash
ldd ./bean_vision_framework | grep "not found"
```

重点确认：

- 输出目录中是否已有 `libonnxruntime.so*`
- 或者 RPATH 是否能正确找到仓库内 ORT 路径

### 4.2 模型路径错误

常见现象：

- `Detector load failed`
- `YOLO model not found`

优先检查：

- 配置中的 `model_path`
- 是否按配置文件目录正确解析

当前项目里实际要求：

- Linux 下模型通常指向 `bisai/bean_digit_v1_verified_20260624/best.onnx`

### 4.3 `best.onnx.data` 缺失

这是当前项目明确记录过的要求：

- `best.onnx` 不能单独存在
- `best.onnx.data` 必须和它在同一目录

如果只拷贝了 `.onnx` 没拷贝 `.data`，模型加载会失败。

### 4.4 检测结果为 0

如果程序能跑起来，但一直 `detections=0`，优先检查：

- 模型是否加载成功
- 输入图像是否正确
- `conf_threshold` 是否过高

不要先怀疑 ROI 或状态机。

## 5. OpenCV 问题

### 5.1 图片读取失败

常见现象：

- `Failed to read image: ...`

说明：

- `input.type=image` 时路径无效
- 应先修正 `input.source`

### 5.2 视频读取问题

当前 `video` 模式走 OpenCV `VideoCapture`。  
如果视频打不开，优先判断：

- 路径是否正确
- 当前 OpenCV 构建是否具备对应视频后端支持

建议先做最小验证：

- 换成 `image` 模式
- 或换成已知可读的视频文件

不要在视频打不开时继续调 Detector。

### 5.3 普通相机与工业相机不要混淆

当前工程同时支持：

- `camera`
- `mindvision_camera`

说明：

- `camera` 走 OpenCV `VideoCapture`
- `mindvision_camera` 走 `CameraManager + MindVision SDK`

工业相机场景下，优先用 `mindvision_camera`，不要默认退回普通 `camera` 模式。

## 6. NUC 部署问题

### 6.1 路径问题

部署到 NUC 后，最常见的问题不是代码逻辑，而是路径不成立：

- 配置路径
- 模型路径
- SDK 路径
- 动态库路径

优先检查：

```bash
ldd ./bean_vision_framework | grep "not found"
ldd ./camera_preview_demo | grep "not found"
```

### 6.2 权限问题

部署阶段需要确认：

- 当前用户对运行目录可写
- 串口设备可访问
- 相机设备具备访问权限

如果现场没有图形界面：

- 关闭 `show_window`
- 只保留日志和调试输出

### 6.3 缺少构建工具

当前工程开发过程中已经明确遇到过：

- 目标环境没有 `cmake`
- 没有 `g++`
- 没有 OpenCV 开发包

这种情况下不要继续调业务逻辑，先补环境。

## 7. 串口调试问题

当前项目已经具备独立串口 demo，因此串口排查建议先脱离主流程：

```bash
./serial_debug_demo --help
./serial_debug_demo --mock
```

### 7.1 设备识别

优先检查：

```bash
ls /dev/ttyACM*
ls /dev/ttyUSB*
```

当前工程常见设备名：

- `/dev/ttyACM0`
- `/dev/ttyUSB0`

### 7.2 权限问题

如果真实串口打不开，应优先检查：

- 当前用户是否能访问对应设备

这类问题通常表现为：

- `Serial open failed`

### 7.3 协议检查

如果串口能打开但联调失败，优先确认：

- 当前是不是 `mock` 模式
- 是否有 ACK
- 是否触发重发
- 包十六进制打印是否开启

当前项目串口联调建议：

- 先跑 `serial_debug_demo`
- 再跑 `debug_image_real_serial`
- 最后再接 `real_robot`

说明：

- 当前 ACK 逻辑已接入
- 但真实 C 板联调仍需双方确认协议细节

## 8. 调试检查清单

程序启动前，建议按这份清单逐项确认：

- [ ] 当前分支正确
- [ ] `cmake` / `g++` / OpenCV / `yaml-cpp` 已安装
- [ ] `build/` 已按当前依赖重新生成
- [ ] ONNX Runtime 路径有效
- [ ] MindVision SDK 路径有效
- [ ] `best.onnx` 与 `best.onnx.data` 同目录
- [ ] 配置文件路径正确
- [ ] `input.type` 与当前调试目标一致
- [ ] 工业相机使用 USB 3.0 直连
- [ ] 不使用 USB 2.0 扩展坞
- [ ] 若在虚拟机中测试，已明确其结果不能替代实体 NUC
- [ ] 串口设备名正确
- [ ] 非图形环境下已关闭 `show_window`
- [ ] 先用最小 demo 验证，再进入完整主流程
