# Config Reference

本文档汇总当前工程配置系统的真实字段与解析规则，只描述当前代码已经支持的配置，不根据 YAML 文件推测不存在的功能。

## 1. 配置体系概述

当前统一配置入口是：

- [include/core/AppConfig.h](/home/ygk/yolo_competition/bean_vision_framework/include/core/AppConfig.h:1)
- [src/core/AppConfig.cpp](/home/ygk/yolo_competition/bean_vision_framework/src/core/AppConfig.cpp:1)

主程序和 demo 通过 `AppConfig::load()` 读取一个入口 YAML，再继续装配其他子配置。

当前 `AppConfig` 实际包含这些子配置：

- `runtime`
- `input`
- `camera`
- `command`
- `scan`
- `detector`
- `roi`
- `serial`
- `debug`

说明：

- 你在任务描述中列出的主块是 `runtime / input / camera / detector / roi / serial / debug`
- 但按当前代码事实，`command` 和 `scan` 也是已实现配置，不应省略

当前相关配置文件包括：

- [config/app.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/app.yaml:1)
- [config/app_linux_test.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/app_linux_test.yaml:1)
- [config/debug_command_image.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/debug_command_image.yaml:1)
- [config/debug_camera_mock.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/debug_camera_mock.yaml:1)
- [config/debug_image_real_serial.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/debug_image_real_serial.yaml:1)
- [config/real_robot.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/real_robot.yaml:1)
- [config/camera_preview_demo.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/camera_preview_demo.yaml:1)
- [config/classes.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/classes.yaml:1)
- [config/roi.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/roi.yaml:1)
- [config/serial.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/serial.yaml:1)

## 2. runtime 配置

结构体：

- `RuntimeConfig`

字段如下：

| 字段 | 类型 | 默认值 | 作用 |
|---|---|---:|---|
| `mode` | `string` | `debug_command_image` | 仅描述当前运行场景名称，不直接决定全部行为；具体差异仍由 `input`、`command`、`serial`、`debug` 等子配置控制 |

当前常见取值：

- `debug_command_image`
- `debug_camera_mock`
- `debug_image_real_serial`
- `real_robot`
- `camera_preview_demo`

说明：

- `mode` 本身主要用于标识运行场景和日志输出
- 当前代码不会仅凭 `mode` 自动推导所有其他配置

## 3. input 配置

结构体：

- `InputConfig`

字段如下：

| 字段 | 类型 | 默认值 | 作用 |
|---|---|---:|---|
| `type` | `string` | `mock` | 输入类型 |
| `source` | `string` | 空 | 图片或视频路径 |
| `camera_id` | `int` | `0` | 摄像头编号，主要用于 `camera` 模式，同时会与 `camera.camera_id` 互相同步 |

### 支持输入类型

当前代码支持：

- `mock`
- `image`
- `video`
- `camera`
- `mindvision_camera`

### 各类型说明

#### `mock`

- 生成一张固定尺寸的空白图
- 不依赖文件和相机

#### `image`

- 从 `source` 读取单张图片
- 适合单帧调试

#### `video`

- 用 OpenCV `VideoCapture` 读取视频文件

#### `camera`

- 用 OpenCV `VideoCapture` 打开普通相机
- 会使用 `camera` 配置中的部分参数

#### `mindvision_camera`

- 使用 `CameraManager` 打开 MindVision 工业相机
- 依赖编译时启用 MindVision SDK

## 4. camera 配置

结构体：

- `CameraConfig`

字段如下：

| 字段 | 类型 | 默认值 | 当前状态 | 说明 |
|---|---|---:|---|---|
| `camera_id` | `int` | `0` | `已实现` | 相机编号 |
| `width` | `int` | `1280` | `部分实现` | 在 OpenCV `camera` 模式中显式使用；在当前 MindVision 路径中未看到显式 SDK 设置调用 |
| `height` | `int` | `720` | `部分实现` | 同上 |
| `fps` | `int` | `30` | `部分实现` | 同上 |
| `auto_exposure` | `bool` | `false` | `已实现` | 自动曝光开关 |
| `exposure_time` | `double` | `8000.0` | `已实现` | 手动曝光时间，单位微秒 |
| `auto_gain` | `bool` | `false` | `部分实现` | 当前主要保留配置语义；MindVision 路径下未看到独立“自动增益开关”SDK 调用 |
| `gain` | `double` | `4.0` | `已实现` | 手动增益值 |
| `auto_white_balance` | `bool` | `false` | `已实现` | 自动白平衡开关 |
| `flip_horizontal` | `bool` | `false` | `已实现` | 输出图像水平翻转 |
| `flip_vertical` | `bool` | `false` | `已实现` | 输出图像垂直翻转 |
| `rotate` | `int` | `0` | `已实现` | 输出图像旋转角度，支持 `0/90/180/270` |

### 推荐 YAML 写法

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
    rotate: 0
```

### 兼容旧写法

当前解析器仍支持：

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

### 解析规则说明

当前 `AppConfig.cpp` 支持：

- 平铺字段
  - `exposure`
  - `gain`
  - `auto_exposure`
  - `auto_gain`
  - `auto_white_balance`
  - `flip_horizontal`
  - `flip_vertical`
  - `rotate`
- 分组字段
  - `exposure.auto`
  - `exposure.time`
  - `gain.auto`
  - `gain.value`
  - `white_balance.auto`
  - `image.flip_horizontal`
  - `image.flip_vertical`
  - `image.rotate`

补充：

- `input.camera_id` 与 `camera.camera_id` 会在解析时同步

## 5. detector 配置

结构体：

- `DetectorConfig`

字段如下：

| 字段 | 类型 | 默认值 | 作用 |
|---|---|---:|---|
| `backend` | `string` | `mock` | 检测后端名称 |
| `model_path` | `string` | 空 | 模型文件路径 |
| `conf_threshold` | `float` | `0.25` | 置信度阈值 |
| `nms_threshold` | `float` | `0.45` | NMS 阈值 |
| `class_file` | `string` | 空 | 类别配置文件路径 |

### 当前真实支持情况

- `backend`
  - 当前配置里常用 `onnxruntime`
  - 结构体注释中写了 `mock / onnxruntime / opencv_dnn`
  - 但当前主流程文档应以已使用的 `onnxruntime` 为主

- `model_path`
  - 会在 `AppConfig::load()` 中解析为相对配置文件目录的实际路径

- `class_file`
  - 用于加载：
    - `names`
    - `aliases`

### `classes.yaml`

当前 `class_file` 指向：

- [config/classes.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/classes.yaml:1)

其中包含：

- `names`
  - 类别 id 到名称的映射
- `aliases`
  - 类别别名归一化映射，例如 `data_1 -> digit_1`

说明：

- `names` 和 `aliases` 不是在入口 YAML 中逐项配置
- 而是通过 `class_file` 间接加载

## 6. roi 配置

结构体：

- `RoiConfig`

字段如下：

| 字段 | 类型 | 作用 |
|---|---|---|
| `pickup_rois` | `map<string, cv::Rect>` | 豆子区域 ROI |
| `place_rois` | `map<string, cv::Rect>` | 数字区域 ROI |

### 入口 YAML 写法

```yaml
roi:
  file: "config/roi.yaml"
```

### 当前 ROI 来源

- [config/roi.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/roi.yaml:1)

当前内容分为两组：

- `pickup_rois`
  - `P1`
  - `P2`
  - `P3`
- `place_rois`
  - `L4`
  - `L5`
  - `L6`
  - `L7`
  - `L8`

说明：

- 当前代码只支持固定坐标 ROI
- 没有多视角 ROI 集切换
- `roi.file` 是当前真实入口，不是直接在主配置里内联写所有矩形

## 7. serial 配置

结构体：

- `SerialConfig`

字段如下：

| 字段 | 类型 | 默认值 | 作用 |
|---|---|---:|---|
| `enable` | `bool` | `false` | 是否启用真实串口 |
| `mock` | `bool` | `true` | 是否只打印 TX 而不实际发送 |
| `port` | `string` | `/dev/ttyACM0` | 串口设备名 |
| `baudrate` | `int` | `115200` | 波特率 |
| `ack_timeout_ms` | `int` | `100` | 等待 ACK 的超时时间 |
| `max_resend` | `int` | `3` | ACK 超时后最大重发次数 |
| `print_packet_hex` | `bool` | `true` | 是否打印协议包十六进制 |
| `print_rx_hex` | `bool` | `false` | 是否打印接收包十六进制 |
| `print_tx_hex` | `bool` | `true` | 是否打印发送包十六进制 |
| `print_parsed_packet` | `bool` | `false` | 是否打印解析后的协议包信息 |

### 两种写法

#### 子文件写法

```yaml
serial:
  file: "config/serial.yaml"
```

对应文件：

- [config/serial.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/serial.yaml:1)

#### 直接内联写法

例如：

```yaml
serial:
  mock: false
  port: "/dev/ttyACM0"
  baudrate: 115200
  ack_timeout_ms: 100
  max_resend: 3
```

### 解析规则说明

- 如果 `serial.file` 存在，则会继续加载子文件
- 主配置中直接写的串口字段也会生效
- 当解析到 `mock` 时，代码会同步设置：
  - `enable = !mock`

### debug 对 serial 的覆盖

在 `AppConfig::load()` 末尾，当前代码会把部分 `debug` 开关同步给 `serial`：

- `serial.print_packet_hex = debug.print_packet_hex`
- `serial.print_rx_hex = debug.print_rx_hex`
- `serial.print_tx_hex = debug.print_tx_hex || debug.print_packet_hex`
- `serial.print_parsed_packet = debug.print_parsed_packet`

因此：

- 串口日志相关行为最终受 `debug` 配置影响

## 8. debug 配置

结构体：

- `DebugConfig`

字段如下：

| 字段 | 类型 | 默认值 | 作用 |
|---|---|---:|---|
| `show_window` | `bool` | `false` | 是否显示 OpenCV 窗口 |
| `draw_result` | `bool` | `true` | 是否在图像上绘制结果 |
| `print_detections` | `bool` | `true` | 是否打印检测结果 |
| `print_roi_result` | `bool` | `true` | 是否打印 ROI 结果 |
| `print_vote_result` | `bool` | `true` | 是否打印多帧投票结果 |
| `print_state` | `bool` | `true` | 是否打印状态机状态变化 |
| `print_packet_hex` | `bool` | `true` | 是否打印协议包十六进制 |
| `print_rx_hex` | `bool` | `false` | 是否打印接收十六进制 |
| `print_tx_hex` | `bool` | `true` | 是否打印发送十六进制 |
| `print_parsed_packet` | `bool` | `false` | 是否打印协议包解析结果 |
| `save_raw_frame` | `bool` | `false` | 是否保存原始输入帧 |
| `save_result_image` | `bool` | `false` | 是否保存绘制结果图 |
| `show_mouse_position` | `bool` | `false` | `camera_preview_demo` 是否显示鼠标坐标 |
| `output_dir` | `string` | `debug_output` | 调试输出目录 |

说明：

- `show_mouse_position` 主要服务于 `camera_preview_demo`
- `output_dir` 会影响调试图保存位置
- 串口相关打印选项最终会同步到 `SerialConfig`

## 9. 配置修改原则

当前建议遵循这些原则：

1. 优先修改 YAML，不直接改代码  
配置系统已经覆盖运行模式、输入、相机、检测、ROI、串口和调试开关，常规调参优先走配置。

2. 修改后重新运行验证  
当前工程没有“热更新所有配置”的统一机制。修改 YAML 后，通常应重新启动对应程序验证。

3. 优先保持现有配置结构  
尤其是：
- `roi.file`
- `serial.file`
- `class_file`
- `camera` 分组写法

4. 不要把未来功能写进当前配置  
当前配置系统不支持：
- 多视角 ROI 集
- 云台视角调度
- pitch / yaw 自动规划

5. 运行路径相关字段要按相对路径规则检查  
`AppConfig::load()` 会优先按配置文件所在目录解析相对路径，因此部署时应保持配置与模型/子配置路径关系稳定。
