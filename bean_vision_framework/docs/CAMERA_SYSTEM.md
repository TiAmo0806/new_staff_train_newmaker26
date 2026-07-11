# Camera System

本文档描述当前工程中已经实现的工业相机模块，只覆盖当前代码事实，不包含未来的多视角管理或云台协同设计。

## 1. 工业相机模块概述

当前工业相机路径为：

```text
InputManager
  -> CameraManager
  -> MindVision SDK
```

对应关系：

- `InputManager`
  - 负责选择输入类型并统一向上层输出 `cv::Mat`
- `CameraManager`
  - 负责 MindVision 工业相机生命周期和图像输出
- MindVision SDK
  - 负责底层设备枚举、打开、取流和释放

### 为什么要拆分

当前拆分的原因是工程职责边界需要清晰：

- `InputManager` 原本同时承担输入源路由和工业相机 SDK 管理，职责过重
- 工业相机参数配置化后，需要独立模块承接 SDK 调用和参数应用
- 上层视觉流程不应直接感知 `CameraApi.h` 或 MindVision SDK 句柄

当前拆分后的边界是：

- `InputManager` 只做输入选择和统一输出
- `CameraManager` 只做工业相机设备管理和图像标准化输出

## 2. CameraManager 职责

头文件：

- [include/camera/CameraManager.h](/home/ygk/yolo_competition/bean_vision_framework/include/camera/CameraManager.h:1)

实现：

- [src/camera/CameraManager.cpp](/home/ygk/yolo_competition/bean_vision_framework/src/camera/CameraManager.cpp:1)

### 当前负责

- 工业相机 SDK 初始化
- 枚举相机设备
- 根据 `camera_id` 打开指定相机
- 获取相机能力信息
- 设置输出格式为 `BGR8`
- 应用当前支持的相机参数
- 触发取流
- 读取一帧图像并输出 `cv::Mat`
- 关闭相机并释放 SDK 资源

### 当前不负责

- ROI 解析
- YOLO 推理
- 云台控制
- 状态机
- 串口协议
- 任务生成

### 与 InputManager 的关系

头文件：

- [include/input/InputManager.h](/home/ygk/yolo_competition/bean_vision_framework/include/input/InputManager.h:1)

实现：

- [src/input/InputManager.cpp](/home/ygk/yolo_competition/bean_vision_framework/src/input/InputManager.cpp:1)

当前关系是：

- `InputManager` 在 `input.type == "mindvision_camera"` 时创建 `CameraManager`
- `InputManager::open()` 调用 `CameraManager::open()`
- `InputManager::read()` 调用 `CameraManager::read()`
- `InputManager::release()` 调用 `CameraManager::close()`

### MindVision SDK 边界

当前代码中：

- `CameraApi.h` 只在 `CameraManager.cpp` 中出现
- 上层模块不直接接触 `CameraHandle`、`CameraSdkInit()` 等 SDK 接口
- `InputManager` 已不再直接调用 MindVision SDK

这条边界是当前相机模块设计的核心。

## 3. CameraConfig 设计

配置结构定义位于：

- [include/core/AppConfig.h](/home/ygk/yolo_competition/bean_vision_framework/include/core/AppConfig.h:1)

当前 `CameraConfig` 字段包括：

- `camera_id`
- `width`
- `height`
- `fps`
- `auto_exposure`
- `exposure_time`
- `auto_gain`
- `gain`
- `auto_white_balance`
- `flip_horizontal`
- `flip_vertical`
- `rotate`

### 字段说明

#### 基础参数

- `camera_id`
  - 相机编号
- `width`
  - 目标宽度配置
- `height`
  - 目标高度配置
- `fps`
  - 目标帧率配置

#### 曝光参数

- `auto_exposure`
  - 是否启用自动曝光
- `exposure_time`
  - 手动曝光时间，单位微秒

#### 增益参数

- `auto_gain`
  - 自动增益开关配置语义
- `gain`
  - 手动增益值

#### 白平衡参数

- `auto_white_balance`
  - 自动白平衡开关

#### 图像方向参数

- `flip_horizontal`
  - 水平翻转
- `flip_vertical`
  - 垂直翻转
- `rotate`
  - 旋转角度，当前支持 `0 / 90 / 180 / 270`

### 当前实现状态说明

当前 MindVision 工业相机路径中，代码明确应用了：

- 自动曝光开关
- 手动曝光时间
- 手动增益
- 自动白平衡开关
- 图像翻转和旋转

当前 `CameraConfig` 中也保留了：

- `width`
- `height`
- `fps`

这些字段目前由配置系统统一承载，并在 OpenCV `camera` 模式中显式使用。  
在当前 `CameraManager` 的 MindVision 路径里，没有看到对应的 SDK 设置调用被显式写入代码。

因此当前文档应理解为：

- 这些字段已经是配置结构的一部分
- 但并非全部都已在 MindVision SDK 路径中显式生效

## 4. YAML 配置说明

当前 `camera` 配置属于 `AppConfig` 的一部分，由：

- [src/core/AppConfig.cpp](/home/ygk/yolo_competition/bean_vision_framework/src/core/AppConfig.cpp:1)

解析。

### 当前推荐写法

示例：

```yaml
camera:
  camera_id: 0
  width: 1280
  height: 720
  fps: 30
  exposure:
    auto: false
    time: 8000
  gain:
    auto: false
    value: 4
  white_balance:
    auto: false
  image:
    flip_horizontal: false
    flip_vertical: false
    rotate: 180
```

这类写法已在当前配置中使用，例如：

- [config/camera_preview_demo.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/camera_preview_demo.yaml:1)
- [config/debug_camera_mock.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/debug_camera_mock.yaml:1)
- [config/real_robot.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/real_robot.yaml:1)

### 兼容旧写法

当前解析器仍兼容旧的平铺写法，例如：

```yaml
camera:
  camera_id: 0
  width: 1280
  height: 720
  fps: 30
  exposure: 8000
  gain: 4
  auto_exposure: false
```

这类写法仍出现在：

- [config/app_linux_test.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/app_linux_test.yaml:1)
- [config/debug_command_image.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/debug_command_image.yaml:1)
- [config/debug_image_real_serial.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/debug_image_real_serial.yaml:1)

说明：

- 当前 `AppConfig` 支持新旧两种相机字段写法
- 新写法更适合继续扩展

## 5. 图像处理流程

当前工业相机图像路径如下：

```text
MindVision SDK 取原始图
  -> SDK 转为 BGR8
  -> OpenCV Mat 封装
  -> clone()
  -> CameraManager 内部方向处理
  -> 输出 cv::Mat
```

当前实现细节：

1. `CameraGetImageBuffer()`
   - 从 SDK 获取一帧原始图像
2. `CameraImageProcess()`
   - SDK 处理到 `BGR8`
3. `cv::Mat`
   - 以 `CV_8UC3` 封装
4. `clone()`
   - 复制成独立输出图像
5. `applyImagePostProcess()`
   - 执行 `flip` / `rotate`

### 为什么方向处理放在 CameraManager

当前设计选择是：

- 固定方向矫正属于相机输出标准化
- 应该在图像离开相机层之前完成
- 这样上层看到的图像方向是统一的

这意味着：

- `ROI`
- `Detector`
- 调试窗口
- 保存图片

看到的是同一方向的图像。

## 6. MindVision SDK 接入说明

当前 SDK 接入依赖：

- `CameraApi.h`
- `libMVSDK.so`

### SDK 位置

当前 CMake 支持：

- 显式指定 `MINDVISION_ROOT`
- 或自动搜索若干常见路径

常见路径由 [CMakeLists.txt](/home/ygk/yolo_competition/bean_vision_framework/CMakeLists.txt:1) 定义，包括：

- `./camera/linuxSDK_V2.1.0.49202602041120`
- `./camera`
- `../linuxSDK_V2.1.0.49202602041120`
- `../camera/linuxSDK_V2.1.0.49202602041120`
- `./third_party/camera/linuxSDK_V2.1.0.49202602041120`
- `./third_party/camera`
- `./sdk/linuxSDK_V2.1.0.49202602041120`
- `./sdk`

### CMake 连接方式

当前 CMake 逻辑是：

- `WITH_MINDVISION=ON` 时尝试启用工业相机支持
- 若找到 `include/CameraApi.h`
  - 定义 `BVP_WITH_MINDVISION`
  - 链接 `MVSDK`
- 若找不到
  - 工程仍可构建，但工业相机路径不可用

### `MINDVISION_ROOT`

如果自动搜索不到，构建时需要显式传入：

```bash
cmake .. -DMINDVISION_ROOT=/path/to/linuxSDK_V2.x.x
```

SDK 根目录至少应包含：

- `include/CameraApi.h`
- `lib/x64/libMVSDK.so`

## 7. 部署注意事项

当前已确认的部署注意事项主要集中在 USB 链路和 SDK 运行环境。

### USB 3.0 要求

工业相机部署时，优先使用：

- USB 3.0 直连接口

### 不建议使用 USB 2.0 扩展坞

根据现有硬件笔记：

- [tools/camera_preview_demo/HARDWARE_DEBUG_NOTE.md](/home/ygk/yolo_competition/bean_vision_framework/tools/camera_preview_demo/HARDWARE_DEBUG_NOTE.md:1)

当前已确认的问题是：

- USB 2.0 扩展坞下可能出现 `lsusb` 可见
- 但 SDK 初始化失败
- 常见日志包括：
  - `user control fd open failed`

当前建议：

- 工业相机优先使用 USB 3.0 直连

### VMware 测试注意

当前工程在虚拟机环境下做过排查，结论是：

- `lsusb` 能看到相机，并不等于 SDK 一定能稳定打开
- VMware USB 透传不能视为与实体 NUC 等价
- 如果相机联调失败，应优先在实体 Ubuntu / NUC 上验证

### 权限与 udev

当前代码本身不处理 udev 规则或设备权限配置。  
部署阶段需要保证：

- 当前用户具有访问相机设备的权限
- 如果现场依赖 udev 规则，应提前安装

这些属于部署与排障层，不属于 `CameraManager` 代码职责。

## 8. 调试入口

本页不展开完整排障步骤。

调试和排障入口建议统一收敛到：

- 规划中的 `DEBUG_GUIDE.md`

当前阶段可先参考：

- [tools/camera_preview_demo/HARDWARE_DEBUG_NOTE.md](/home/ygk/yolo_competition/bean_vision_framework/tools/camera_preview_demo/HARDWARE_DEBUG_NOTE.md:1)
- [docs/DEPLOYMENT.md](/home/ygk/yolo_competition/bean_vision_framework/docs/DEPLOYMENT.md:1)
- [docs/BUILD_AND_RUN.md](/home/ygk/yolo_competition/bean_vision_framework/docs/BUILD_AND_RUN.md:1)
