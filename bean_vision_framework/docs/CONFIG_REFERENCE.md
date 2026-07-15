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
- `preview`

说明：

- 你在任务描述中列出的主块是 `runtime / input / camera / detector / roi / serial / debug`
- 但按当前代码事实，`command` 和 `scan` 也是已实现配置，不应省略
- `preview` 也已实现，主要用于 `camera_preview_demo`

当前相关配置文件包括：

- [config/app.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/app.yaml:1)
- [config/app_linux_test.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/app_linux_test.yaml:1)
- [config/debug_command_image.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/debug_command_image.yaml:1)
- [config/debug_camera_mock.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/debug_camera_mock.yaml:1)
- [config/debug_image_real_serial.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/debug_image_real_serial.yaml:1)
- [config/real_robot.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/real_robot.yaml:1)
- [config/camera_preview_demo.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/camera_preview_demo.yaml:1)
- [config/camera_yolo_preview.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/camera_yolo_preview.yaml:1)
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
- `camera_yolo_preview`

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

### 检测阈值调参说明

#### `conf_threshold`

含义：

- `YOLO` 单帧检测置信度阈值
- 低于该阈值的检测框会被过滤
- 取值范围通常是 `0.0 ~ 1.0`

调低的效果：

- 更容易看到弱检测框
- 适合找角度、验证模型是否对目标有反应
- 但误检通常会增加

调高的效果：

- 误检更少
- 输出结果更干净
- 但侧面数字、暗光目标、小目标可能被直接过滤掉

推荐值：

- 找侧面数字角度：`0.15 ~ 0.25`
- 正常调试：`0.25 ~ 0.35`
- 稳定运行：`0.4 ~ 0.6`，最终仍要以模型实际表现为准

注意：

- `conf_threshold` 只决定单帧 `YOLO` 检测框是否输出
- 它不同于多帧投票阶段的平均置信度阈值 `scan.min_avg_confidence`

#### `nms_threshold`

含义：

- `NMS` 非极大值抑制阈值
- 用来删除同一个目标的重复检测框
- 判断依据是检测框之间的重叠程度 `IoU`

调低的效果：

- 更容易删除重复框
- 同一个数字出现多个框时可以优先尝试调低
- 但目标彼此靠得近时可能误删相邻目标

调高的效果：

- 更不容易删框
- 靠近目标不容易被误删
- 但同一个目标的重复框可能会变多

推荐值：

- 默认值：`0.45`
- 重复框较多：可尝试 `0.35`
- 靠近目标被误删：可尝试 `0.55`

简单记忆：

- `conf_threshold` 控制“低置信度框要不要”
- `nms_threshold` 控制“重叠重复框要不要”

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

### ROI 调参方法

ROI 基本格式：

- ROI 使用 `[x, y, w, h]`
- `x / y` 是左上角坐标
- `w / h` 是宽和高
- 图像坐标系左上角是 `(0,0)`
- `x` 向右增大，`y` 向下增大

当前解析规则：

- 当前 `RoiParser` 不是看目标框是否整体落入 ROI
- 而是根据检测框中心点是否落入 ROI 来判断归属
- 因此 ROI 不一定要完整包住目标外接框
- 更关键的是覆盖检测框中心点的活动范围

调参建议：

- 如果目标能出框，但总是分不到正确位置，先检查检测框中心点轨迹
- ROI 优先围绕“中心点稳定落点区域”调整，而不是机械追求框住整块目标
- 当中心点经常压边时，可适当增大 `w / h`
- 当相邻位置容易串位时，应优先细调 `x / y`，必要时缩小 ROI 重叠区域

`offset` 日志解释：

日志中的：

```text
offset=(-105,59)
```

表示：

- `检测框中心点 - ROI 中心点`

调参方向：

- `offset x < 0`：检测中心在 ROI 中心左侧，ROI 可考虑向左移
- `offset x > 0`：检测中心在 ROI 中心右侧，ROI 可考虑向右移
- `offset y < 0`：检测中心在 ROI 中心上方，ROI 可考虑向上移
- `offset y > 0`：检测中心在 ROI 中心下方，ROI 可考虑向下移

### `single_view_4` / 三视角 ROI 说明

当前代码要求 ROI 文件必须保留以下固定键：

- `P1 / P2 / P3`
- `L4 / L5 / L6 / L7 / L8`

说明：

- 即使某个视角当前看不到 `L8`，也不能删除 `L8` 这个键
- `RoiParser` 会直接按固定键访问这些位置，缺键不是“关闭该位”，而是配置不完整

`single_view_4` 临时方案：

- 真实可见的 `4` 个 `L` 位正常填写
- 暂时不可见的 `1` 个 `L` 位仍保留占位 ROI
- 占位 ROI 可以放在低风险空白区域，例如 `[0, 0, 1, 1]`
- 目的是让该位置在当前视角下持续保持 `unknown`，再配合后续 `4 -> 5` 补全逻辑

三视角 `view_1 / view_2 / view_3`：

- 当前仓库里这些 ROI 文件只是手动调试预设
- 目前还没有自动三视角融合
- 不能通过删除某个 `L` 位来实现所谓“3 ROI 模式”
- 若后续要做正式三视角方案，仍需要 `view_id`、`DigitViewMemory` 和多视角融合逻辑配合

## 7. preview 配置

结构体：

- `PreviewConfig`

字段如下：

| 字段 | 类型 | 默认值 | 作用 |
|---|---|---:|---|
| `draw_roi` | `bool` | `true` | `camera_preview_demo` 中是否绘制 ROI |
| `yolo_enable` | `bool` | `false` | `camera_preview_demo` 中是否启用 YOLO 实时检测 |
| `draw_boxes` | `bool` | `true` | YOLO 启用时是否绘制检测框 |
| `print_detections` | `bool` | `false` | YOLO 启用时是否在终端打印检测结果 |

示例：

```yaml
preview:
  draw_roi: true
  yolo_enable: true
  draw_boxes: true
  print_detections: true
```

说明：

- `preview` 主要服务 `camera_preview_demo`
- 不参与主程序 `bean_vision_framework` 的状态机流程
- `yolo_enable=true` 时只做实时检测和画框
- 不会进入 `TaskStateMachine`
- 不会生成 `VisionResult`
- 不会发送串口
- 不要求 `P1 / P2 / P3` 或 `L4 ~ L8` 识别完整才继续

### 相机 + YOLO 预览调角度

用途：

- 寻找侧面数字最佳识别角度
- 观察模型是否能检出 `digit`
- 观察检测框和置信度是否稳定
- 不走任务状态机

推荐运行：

```bash
./camera_preview_demo ../config/camera_yolo_preview.yaml
```

推荐初始参数：

```yaml
detector:
  conf_threshold: 0.25
  nms_threshold: 0.45

preview:
  draw_roi: true
  yolo_enable: true
  draw_boxes: true
  print_detections: true
```

调试侧面数字时建议：

- 先把 `conf_threshold` 设到 `0.15 ~ 0.25`
- 先确认模型是否至少有检测反应
- 找到能稳定出框的角度后，再逐步提高 `conf_threshold`
- `nms_threshold` 一般先保持 `0.45`
- 只有重复框很多时再调 `nms_threshold`

## 8. serial 配置

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

## 9. debug 配置

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

## 10. 常见调参场景

### 场景 A：侧面数字完全没有框

建议：

- 先把 `conf_threshold` 降到 `0.15 ~ 0.25`
- 优先使用 `camera_yolo_preview.yaml`
- 调整相机角度、目标距离、曝光和补光
- 这个阶段先不要依赖主状态机判断最终结果

### 场景 B：数字有框但进不了 ROI

建议：

- 重点看日志里的 `center` 和 `offset`
- 调整 ROI 的 `[x, y, w, h]`
- 目标是让检测框中心点稳定落入对应 ROI

### 场景 C：同一个数字出现多个框

建议：

- 适当降低 `nms_threshold`，例如从 `0.45` 调到 `0.35`
- 如果因此误删了相邻目标，再调回 `0.45` 或尝试 `0.55`

### 场景 D：误检太多

建议：

- 提高 `conf_threshold`
- 缩小 ROI，或把 ROI 调整到目标中心点真正活动的区域
- 提高多帧投票门槛
- 同时检查光照、反光和背景干扰

### 场景 E：某一帧成功但整轮失败

解释：

- 单帧 `parsed VisionResult: success` 不等于整轮多帧投票成功
- 还要继续看 `ROI-VOTE` 日志里的 `count / avg_conf`
- 只有多帧统计稳定通过后，流程才会进入下一状态

### 场景 F：相机画面太暗或不稳定

建议：

- 调整 `exposure.time`
- 调整 `gain.value`
- 尽量固定白平衡
- 保证补光稳定
- 先用 `camera_preview_demo` 看实时画面，再跑主流程

## 11. 配置修改原则

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
