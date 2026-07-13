# REAL_ROBOT_PIPELINE 阶段性总结

## 1. 当前分支目标

当前分支 `feature_real_robot_pipeline` 的目标，是把视觉识别、任务生成、串口协议、STM32 ACK 闭环接成一条可运行的基础业务链路，形成从“到位命令”到“最终任务下发”的最小可用实现。

本阶段关注的是基础闭环，不追求复杂容错和完整比赛期运行保障。

## 2. 当前完成能力

当前已经完成并接通：

- `ARRIVE_BEAN` 触发豆子识别
- 识别 `P1/P2/P3` 豆子类别
- 生成并发送 `BEAN_BIND`
- 等待并接收 STM32 `ACK`
- `ARRIVE_DIGIT` 触发数字识别
- 识别 `L4-L8` 数字
- 支持数字区 `4 -> 5` 缺失补全
- 合并豆子结果和数字结果
- `TaskGenerator` 生成 3 条搬运任务
- 生成并发送 `FINAL_TASK`
- 等待并接收 STM32 `ACK`
- ACK 成功后进入 `DONE`

已验证链路：

`ARRIVE_BEAN -> BEAN_BIND -> ACK -> ARRIVE_DIGIT -> DigitInference -> FINAL_TASK -> ACK -> DONE`

## 3. 主运行链路

当前主链路分两段：

### 3.1 豆子阶段

`ARRIVE_BEAN`
-> `SerialCommandSource` 或终端命令
-> `TaskStateMachine::handleArriveBean()`
-> `RecognitionRunner::scanBeans()`
-> `VisionMemory::updateBeans()`
-> `VisionMemory::beanBinds()`
-> `Protocol::makeBeanBindPacket()`
-> `SerialPort::write()`
-> 等待 `ACK`
-> `WAIT_DIGIT_COMMAND`

### 3.2 数字与最终任务阶段

`ARRIVE_DIGIT`
-> `TaskStateMachine::handleArriveDigit()`
-> `RecognitionRunner::scanDigits()`
-> `DigitInference::analyze()`
-> `applyInferredDigits()`
-> `mergeBeanAndDigitResult()`
-> `TaskGenerator::generate()`
-> `printTaskPreview()`
-> `Protocol::makeTaskPacket()`
-> `SerialPort::write()`
-> 等待 `ACK`
-> `DONE`

## 4. 串口命令和 payload 格式

当前统一协议帧格式为：

`A5 | cmd | length | seq | payload... | crc_l | crc_h`

当前主流程涉及的命令字：

- `0x10` `ARRIVE_BEAN`
- `0x11` `ARRIVE_DIGIT`
- `0x12` `RESET`
- `0x02` `FINAL_TASK`
- `0x04` `BEAN_BIND`
- `0x14` `ACK`
- `0x13` `PING`
- `0x05` `PONG`

当前 ACK 规则：

- 算法侧收到 `ARRIVE_BEAN / ARRIVE_DIGIT / RESET` 后立即回 `ACK`
- 算法侧收到 `PING` 后回 `PONG`，不回 `ACK`
- 算法侧发送 `BEAN_BIND / FINAL_TASK` 后，由 `SerialPort::write()` 负责等待 `ACK`
- `ACK payload = [acked_cmd, acked_seq]`

注意：

- `runtime.mode` 只是日志和场景标签，不直接决定业务行为
- 真正决定行为的是 `input.type`、`command.source`、`serial.mock`、`detector.backend`

## 5. BEAN_BIND 格式

`BEAN_BIND` 由 `Protocol::makeBeanBindPacket()` 生成。

payload 固定 10 字节：

- `payload[0]`: 绑定数量，最大 3
- 后续 3 组，每组 3 字节：`pickup, bean, target_digit`

编码规则：

- `pickup`: `P1=1`, `P2=2`, `P3=3`
- `bean`: `soybean=0`, `mung_bean=1`, `white_kidney_bean=2`
- `target_digit`: `digit_1=1`, `digit_2=2`, `digit_3=3`, `digit_4=4`, `digit_5=5`

不足 3 组时补位：

- `pickup=0`
- `bean=255`
- `target_digit=0`

业务映射规则：

- `soybean -> digit_1`
- `mung_bean -> digit_2`
- `white_kidney_bean -> digit_3`

## 6. FINAL_TASK 格式

`FINAL_TASK` 由 `Protocol::makeTaskPacket()` 生成。

payload 固定 11 字节：

- `payload[0]`: 状态，成功为 `1`
- `payload[1]`: 任务数量，最大 3
- 后续 3 组，每组 3 字节：`from, to, bean`

编码规则：

- `from`: `P1=1`, `P2=2`, `P3=3`
- `to`: `L4=4`, `L5=5`, `L6=6`, `L7=7`, `L8=8`
- `bean`: `soybean=0`, `mung_bean=1`, `white_kidney_bean=2`

不足 3 条任务时补 `0`

当前联调示例：

- `P1 -> L6 bean=0`
- `P2 -> L5 bean=1`
- `P3 -> L8 bean=2`

## 7. DigitInference 4→5 补全规则

当前 `DigitInference` 的最小补全逻辑是：

- 只处理 `L4-L8` 五个位置
- 如果五个位置全部识别到数字，直接认为完整
- 如果刚好缺失 1 个位置，且另外 4 个位置识别结果互不重复，且都在 `1..5`
- 则从集合 `{1,2,3,4,5}` 中找出唯一缺失数字
- 把该数字补到唯一缺失位置
- 本阶段联调已验证 `4 -> 5` 补全能工作

当前不会处理：

- 缺失两个及以上位置
- 出现重复数字后的复杂推断
- 多视角融合

## 8. debug_image_real_serial 使用方法

用途：

- 图片输入
- 真串口收命令、发业务包
- 适合在不接真实工业相机时联调协议和主流程

关键点：

- `runtime.mode=debug_image_real_serial` 只是标签
- 真正起作用的是这组组合：
- `input.type=image`
- `command.source=serial`
- `serial.mock=false`
- `detector.backend=onnxruntime`

建议步骤：

1. 编译主程序和 demo。
2. 确认串口设备号，例如 `/dev/ttyACM0`。
3. 检查 `config/debug_image_real_serial.yaml` 中的模型、串口、ROI 路径。
4. 运行主程序并加载该配置。
5. 由 STM32 发 `ARRIVE_BEAN` / `ARRIVE_DIGIT`，观察 ACK、`BEAN_BIND`、`FINAL_TASK` 日志。

典型配置语义：

- 图片来自本地文件
- 到位命令来自串口
- 发送走真实串口
- 适合做协议与业务闭环验证

## 9. real_robot 使用方法

用途：

- 真实机器人场景
- 真实相机输入
- 真串口命令与回包

需要明确：

- `real_robot` 是运行场景名，不是行为开关
- 真正决定是否是“真实相机 + 真串口”的，是 `input.type`、`command.source`、`serial.mock`
- 上车时应确保是“相机输入 + 串口命令 + 真串口发送”的组合

推荐目标组合：

- `input.type=mindvision_camera` 或现场实际相机类型
- `command.source=serial`
- `serial.mock=false`
- `detector.backend=onnxruntime`

当前仓库里的 `config/real_robot.yaml` 已明确采用：

- `command.source=serial`
- `serial.mock=false`

上车前需要重点确认：

- `input.type` 是否已经切到真实相机模式
- 工业相机 SDK 是否可用
- ROI 是否与现场机位一致
- 串口设备名是否正确

## 10. 已验证测试项

最近阶段联调已验证：

- NUC 可接收 STM32 发来的 `ARRIVE_BEAN`
- NUC 可对 `ARRIVE_BEAN` 回 `ACK`
- 豆子识别后可生成 `BEAN_BIND`
- `BEAN_BIND` 可发到 STM32
- STM32 可对 `BEAN_BIND` 回 `ACK`
- NUC 可接收 STM32 发来的 `ARRIVE_DIGIT`
- NUC 可对 `ARRIVE_DIGIT` 回 `ACK`
- 数字识别链路可跑通
- `DigitInference` 的单点缺失补全可工作
- `TaskGenerator` 可生成最终任务
- `FINAL_TASK` 可发到 STM32
- STM32 可对 `FINAL_TASK` 回 `ACK`
- 状态机在 `FINAL_TASK ACK` 成功后进入 `DONE`

## 11. 已知现象

- `runtime.mode` 名字看起来像“运行模式”，但当前主要是日志/配置标签，不是核心行为开关
- 当前没有自动重连
- C 板插拔后，串口设备可能从 `/dev/ttyACM0` 变成 `/dev/ttyACM1`
- 若串口号变化而配置未更新，会直接导致打开串口失败
- `SerialPort::write()` 已负责 ACK 等待和重发，但状态机本身不做更复杂恢复
- 若 `FINAL_TASK` 发送失败或 ACK 超时，当前会回 `WAIT_DIGIT_COMMAND`
- 若 `BEAN_BIND` 发送失败或 ACK 超时，当前会回 `WAIT_BEAN_COMMAND`

## 12. 暂未完成事项

本阶段明确未完成：

- 自动串口重连
- 热插拔自动识别新串口号
- `HEARTBEAT`
- `READY`
- `WATCHDOG`
- 多视角融合
- 更复杂的数字推断
- 更复杂的异常恢复
- 长时间运行稳定性治理

## 13. 上车调试注意事项

- 不要把 `runtime.mode` 当成真实行为来源，现场问题优先检查 `input.type / command.source / serial.mock / detector.backend`
- 上车前先确认 `/dev/ttyACM0` 是否仍然成立，必要时检查是否变成 `/dev/ttyACM1`
- 现场接线和上电顺序变化后，要重新确认串口设备名
- 当前没有自动重连，串口掉线后通常需要重启程序
- 上车前先独立验证相机，再独立验证串口，再合主流程
- 不要删除 `serial_debug_demo`
- 不要删除 `serial_protocol_demo`
- 不要删除 `camera_preview_demo`
- 这三个 demo 是现场排障和问题切分的重要工具
- 若主流程异常，优先用 `camera_preview_demo` 查相机和 ROI
- 再用 `serial_protocol_demo` / `serial_debug_demo` 查串口收发和 ACK
- 最后再回到 `debug_image_real_serial` 或 `real_robot` 做整链验证

## 14. 后续分支建议

建议下一阶段分支按风险拆开，不要继续在主链路上堆功能：

- `feature_serial_reconnect`
  目标：补自动重连、端口变化检测、串口恢复策略
- `feature_runtime_stability`
  目标：补 `HEARTBEAT`、在线性判断、超时治理
- `feature_real_robot_camera_integration`
  目标：收敛真实工业相机配置、SDK 依赖、ROI 标定和现场参数
- `feature_digit_inference_enhancement`
  目标：扩展多缺失位和重复数字场景下的推断策略
- `feature_error_recovery`
  目标：补状态机异常回退和重试策略

## 结论

`feature_real_robot_pipeline` 当前已经完成“视觉识别 -> 协议下发 -> STM32 ACK -> DONE”的基础业务闭环，具备继续上车联调和稳定性迭代的基础。

本阶段最重要的结论不是“全部完成”，而是：

- 主协议通了
- 主状态机通了
- `FINAL_TASK` 实发和 ACK 闭环通了
- 现场下一步应把重点转到稳定性、重连、真实相机配置和排障工具保留上
