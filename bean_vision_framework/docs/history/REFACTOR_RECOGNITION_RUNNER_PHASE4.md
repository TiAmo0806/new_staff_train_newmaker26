# RecognitionRunner Refactor Phase 4

本文档记录 RoboMaster 视觉工程在 `RecognitionRunner` 接入过程中的阶段性重构结果。  
它不是最终架构说明文档，重点是说明：

```text
RecognitionRunner 已进入主程序生命周期
+ Bean 识别流程已完成第一次迁移
+ 状态机与识别执行逻辑开始解耦
```

当前内容描述的是 Phase 4.1 的实际工程状态，不代表数字流程已经完成迁移。

## 1. 背景

在这一阶段之前，`TaskStateMachine` 直接负责：

- 输入获取
- `Detector` 调用
- `RoiParser` 调用
- `VisionResult` 生成

这导致状态机同时承担：

- 什么时候识别
- 如何识别

两层职责，进而造成状态控制与视觉执行逻辑耦合。

因此工程中引入：

- `RecognitionRunner`

目标是把：

- “什么时候识别”

和：

- “如何完成一次识别”

明确分离。

## 2. 当前完成内容

### Phase 1

完成：

- `InputManager` 生命周期解耦
- 增加 `reset()` 接口

### Phase 2

完成：

- 建立 `RecognitionRunner` 抽象层

当前抽象链路为：

```text
InputManager
  -> RecognitionRunner
  -> Detector
  -> RoiParser
  -> VisionResult
```

### Phase 3

完成：

- `RecognitionRunner` 纳入 `main` 生命周期管理

对象关系变为：

```text
main
  -> InputManager
  -> Detector
  -> RoiParser
  -> RecognitionRunner
  -> TaskStateMachine
```

### Phase 4.1

完成：

- Bean 识别流程迁移到 `RecognitionRunner`

说明：

- 本阶段只迁移 Bean 流程
- 数字流程保持旧路径
- 串口协议、任务生成和 ACK 机制均未改动

## 3. Bean 流程变化

### 修改前

```text
ARRIVE_BEAN
  -> TaskStateMachine
  -> cv::imread()
  -> Detector
  -> RoiParser
  -> VisionResult
  -> BEAN_BIND
```

### 修改后

```text
ARRIVE_BEAN
  -> TaskStateMachine
  -> RecognitionRunner.scanBeans()
  -> InputManager
  -> Detector
  -> RoiParser
  -> VisionResult
  -> BEAN_BIND
```

### 当前迁移结果

`handleArriveBean()` 中已经不再直接负责：

- 读图
- 检测
- ROI 解析

而是改为：

- 请求 `RecognitionRunner` 完成一次 Bean 识别
- 获取 `VisionResult`
- 继续执行原有 Bean 结果缓存和发送流程

## 4. 当前对象关系

当前主程序对象关系为：

```text
main
  -> InputManager
  -> BeanNumberDetector
  -> RoiParser
  -> RecognitionRunner
  -> TaskGenerator
  -> TaskStateMachine
  -> Protocol
  -> SerialPort
```

其中：

- `TaskStateMachine` 持有 `RecognitionRunner&`
- `TaskStateMachine` 不拥有 runner
- `TaskStateMachine` 不负责创建 runner
- `TaskStateMachine` 不负责释放 runner

## 5. 生命周期变化

### 修改前

状态机直接依赖：

- `Detector`
- `RoiParser`

并在 Bean 图片模式下自行完成：

- 输入读取
- 单帧检测
- ROI 解析

### 修改后

状态机改为依赖：

- `RecognitionRunner`

职责重新划分为：

#### RecognitionRunner 负责

- 输入读取
- 检测执行
- ROI 解析
- 返回当前一次观测结果

#### TaskStateMachine 负责

- 命令处理
- 状态切换
- 结果缓存
- 协议发送

这意味着状态机开始从“既管流程又管识别”转向“只管流程和结果消费”。

## 6. 当前保留未迁移部分

以下路径在 Phase 4.1 之后仍保持旧流程。

### 数字识别

当前仍然是：

```text
handleArriveDigit()
  -> cv::imread()
  -> Detector
  -> RoiParser
```

暂不迁移原因：

- 数字识别后续可能涉及固定云台方案
- 可能需要多视角观测
- 可能出现部分数字缺失
- 后续需要 `DigitInference`

因此 `scanDigits()` 不能简单等价成“得到完整最终任务结果”。

### CameraCommand

当前相机命令入口仍然使用：

- `MultiFrameRecognizer`

还没有统一切到：

- `RecognitionRunner`

后续如果继续推进输入层与识别层统一，这一块会是下一步迁移点之一。

## 7. 当前输入行为变化

这一阶段最重要的行为变化是：

Bean 命令：

```text
arrive_bean image_path
```

在命令格式上仍然保留 `image_path`，但是其意义已经发生变化。

当前：

- `image_path` 仍然保留在终端命令格式里
- `image_path` 仍然参与日志输出
- `image_path` 不再决定 Bean 流程真正读取哪张图

Bean 流程真实输入已经改为由：

```text
RecognitionRunner
  -> InputManager
```

决定。

因此当前工程处于一个过渡状态：

- Bean 流程：已走新的输入抽象路径
- Digit 流程：仍走旧的命令携带图片路径

## 8. 当前运行方式

当前运行方式保持不变：

### 启动方式

```bash
bean_vision_framework <config.yaml>
```

### 配置方式

- 不变

### 串口协议

- 不变

### 返回格式

- 不变

### ACK 机制

- 不变

本阶段的重构重点仅限于：

- Bean 识别执行入口

没有改动业务协议或状态机业务规则。

## 9. 验证计划

Phase 4.1 完成后，应进行以下验证。

### 1. 编译

```bash
cmake --build build -j$(nproc)
```

### 2. 图片模式

验证链路：

```text
ARRIVE_BEAN
  -> RecognitionRunner
  -> BEAN_BIND
```

重点确认：

- 状态机是否进入 `SCAN_BEANS`
- Bean 识别是否经由 `RecognitionRunner`
- `BEAN_BIND` 是否仍能正确生成

### 3. 真串口模式

验证链路：

```text
STM32
  -> ARRIVE_BEAN
  -> 视觉识别
  -> BEAN_BIND
  -> ACK
```

重点确认：

- 串口命令触发是否仍然有效
- 协议和 ACK 是否保持不变
- Bean 迁移后是否仍能形成闭环

## 10. 下一阶段

下一阶段是：

- Phase 4.2：数字识别接入

但数字流程不能直接照搬 Bean 流程改法。

原因是数字阶段后续还需要重新设计：

- 部分数字观测
- 多视角融合
- 数字推理

因此未来更合理的结构应是：

```text
RecognitionRunner
  -> DigitObservation
  -> DigitInference
  -> TaskGenerator
```

而不是把：

- `scanDigits()`

直接等价成：

- “生成完整数字任务结果”

## 11. 当前状态总结

### 已完成

- `CameraManager`
- 串口基础通信
- `RecognitionRunner` 抽象
- Bean 识别 Runner 迁移

### 待完成

- Bean 真串口闭环验证
- Digit 流程迁移
- 数字推理模块设计
- 完整比赛流程闭环

## 12. 当前阶段结论

Phase 4.1 的价值不在于直接完成整个识别链重构，而在于建立了一个明确的工程分界：

以前：

```text
TaskStateMachine
  -> 输入
  -> 识别
  -> 流程
```

现在：

```text
TaskStateMachine
  -> RecognitionRunner
       -> InputManager
       -> Detector
       -> RoiParser
```

这一步完成后，Bean 闭环已经具备了通过统一识别执行层运行的基础，后续数字流程、推理模块和多视角扩展都可以在这个分层上继续推进。
