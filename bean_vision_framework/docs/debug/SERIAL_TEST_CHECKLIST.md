# SERIAL_TEST_CHECKLIST

## 1. 目标与适用范围

本文档用于：

- NUC 视觉程序与 STM32 C 板串口联调；
- 验证 `ARRIVE_BEAN / ARRIVE_DIGIT / BEAN_BIND / FINAL_TASK / ACK`；
- 验证 `port:auto`、ACK 超时、错误帧、断联现象；
- 上车现场记录问题和结论。

适用阶段：

- `feature_serial_stability`
- 后续实车调试分支

## 2. 上车前准备

- 确认当前分支为 `feature_serial_stability` 或后续实车调试分支；
- 确认工程已编译；
- 确认 C 板 USB 已连接；
- 确认串口设备存在，例如 `/dev/ttyACM0`、`/dev/ttyACM1`；
- 确认配置中 `serial.mock=false`；
- 确认 `serial.port` 可设置为 `/dev/ttyACM0` 或 `auto`；
- 确认 `logs/` 目录用于保存日志；
- 确认电控端支持发送 `ARRIVE_BEAN`、`ARRIVE_DIGIT`、`RESET`、`PING`；
- 确认电控端支持解析 `BEAN_BIND` 和 `FINAL_TASK` 并回 `ACK`。

建议现场先检查：

```bash
git branch --show-current
git rev-parse --short HEAD
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
```

## 3. 环境信息记录

- 日期：
- 测试地点：
- Git 分支：
- Git commit：
- NUC：
- C 板固件版本：
- 配置文件：
- 串口设备：
- 测试人员：
- 备注：

## 4. 常用运行命令

详见 [docs/runtime/RUN_COMMANDS.md](/home/ygk/yolo_competition/bean_vision_framework/docs/runtime/RUN_COMMANDS.md:1)。

最常用两个：

```bash
./scripts/run_debug_image_real_serial.sh
./scripts/run_real_robot.sh
```

说明：

- `debug_image_real_serial` 用于图片输入 + 真串口；
- `real_robot` 用于真实相机 + 真串口；
- 日志会保存到 `logs/`；
- 如果脚本内部没有 `sudo`，现场通常仍需要在外层使用 `sudo` 运行真实串口模式。

## 5. 测试项模板

### 测试项名称

测试目的：  
前置条件：  
电控发送内容：  
视觉期望日志：  
电控期望现象：  
通过标准：  
失败排查方向：  
实测记录：  
结论：  

## 6. 基础连通性测试

### 6.1 显式端口模式

测试目的：验证固定端口模式可正常打开串口。

前置条件：

- 配置 `serial.port=/dev/ttyACM0`；
- `serial.mock=false`；
- C 板已连接。

电控发送内容：

- 无，先只验证程序启动。

视觉期望日志：

- `Serial opened: /dev/ttyACM0 baudrate=115200`

通过标准：

- 程序成功启动；
- 无 `Serial open failed`。

失败排查方向：

- 检查 USB 连接；
- 检查串口权限；
- 检查设备名是否变成 `/dev/ttyACM1`；
- 检查端口是否被其他进程占用。

### 6.2 auto 模式

测试目的：验证 `serial.port=auto` 时可自动选择可用端口。

前置条件：

- 配置 `serial.port=auto`；
- `serial.mock=false`；
- C 板已连接。

电控发送内容：

- 无，先只验证程序启动。

视觉期望日志：

- `Serial opened: /dev/ttyACM0 baudrate=115200`
- 或 `[SERIAL] auto selected port=/dev/ttyACM1`

通过标准：

- 程序成功启动；
- auto 模式能选中实际存在的端口。

失败排查方向：

- 检查 USB 是否识别；
- 检查 `/dev/ttyACM*` 是否存在；
- 检查权限；
- 检查是否错误连接到了其他设备。

## 7. ARRIVE_BEAN -> BEAN_BIND 测试

测试目的：验证豆子阶段完整收发链路。

前置条件：

- 串口已正常打开；
- 电控具备发送 `ARRIVE_BEAN` 和回复 `ACK` 的能力；
- 视觉输入已准备好。

电控发送内容：

- `ARRIVE_BEAN`，`CMD=0x10`

视觉期望行为：

- 立即回复 `ACK`
- 进入 `SCAN_BEANS`
- 识别豆子
- 发送 `BEAN_BIND`
- 等待并收到电控 `ACK`
- 进入 `WAIT_DIGIT_COMMAND`

电控期望现象：

- 收到视觉对 `ARRIVE_BEAN` 的 `ACK`
- 收到 `BEAN_BIND`
- 能正确解析 payload
- 对 `BEAN_BIND` 回 `ACK`

期望日志关键词：

```text
[RX COMMAND] ARRIVE_BEAN
[STATE] SCAN_BEANS
[STATE] SEND_BEAN_BIND
[TX PARSED] cmd=BEAN_BIND
[ACK] received for BEAN_BIND
[STATE] WAIT_DIGIT_COMMAND
```

通过标准：

- `ARRIVE_BEAN` 入站后视觉有 ACK；
- `BEAN_BIND` 成功发送；
- 电控正确回 ACK；
- 视觉进入 `WAIT_DIGIT_COMMAND`。

失败排查方向：

- 电控是否真的发了 `ARRIVE_BEAN`；
- 电控是否收到了视觉回的 ACK；
- 豆子识别是否为空；
- 电控是否能解析 `BEAN_BIND`；
- ACK 的 `cmd/seq` 是否匹配。

## 8. ARRIVE_DIGIT -> FINAL_TASK 测试

测试目的：验证数字阶段到最终任务下发的完整链路。

前置条件：

- 已完成豆子阶段；
- 状态在 `WAIT_DIGIT_COMMAND`；
- 电控具备发送 `ARRIVE_DIGIT` 和回复 `ACK` 的能力。

电控发送内容：

- `ARRIVE_DIGIT`，`CMD=0x11`

视觉期望行为：

- 立即回复 `ACK`
- 进入 `SCAN_DIGITS`
- 输出数字识别日志
- 必要时触发 `4 -> 5` 补全
- `TaskGenerator` 生成任务
- 发送 `FINAL_TASK`
- 收到电控 `ACK`
- 进入 `DONE`

电控期望现象：

- 收到视觉对 `ARRIVE_DIGIT` 的 `ACK`
- 收到 `FINAL_TASK`
- 能正确解析 payload
- 对 `FINAL_TASK` 回 `ACK`

期望日志关键词：

```text
[RX COMMAND] ARRIVE_DIGIT
[STATE] SCAN_DIGITS
[DIGIT]
[TASK PREVIEW] success
[STATE] SEND_FINAL_TASK
[TX PARSED] cmd=FINAL_TASK
[ACK] received for FINAL_TASK
[STATE] DONE
```

通过标准：

- `ARRIVE_DIGIT` 入站后视觉有 ACK；
- `FINAL_TASK` 发送成功；
- 电控正确回 ACK；
- 视觉最终进入 `DONE`。

失败排查方向：

- 数字识别是否完整；
- 是否只识别到 4 个数字，且补全是否生效；
- `TaskGenerator` 是否成功；
- 电控是否正确解析 `FINAL_TASK`；
- ACK 的 `cmd/seq` 是否匹配。

## 9. ACK 超时与重发测试

### 9.1 BEAN_BIND 后不回 ACK

测试目的：验证 `BEAN_BIND` ACK 超时和重发日志。

电控发送内容：

- 正常发送 `ARRIVE_BEAN`
- 收到 `BEAN_BIND` 后故意不回 ACK

视觉期望日志：

```text
[WARN] ACK timeout
[ERROR] ACK failed
```

通过标准：

- 视觉不会误判发送成功；
- 不进入正常数字阶段。

### 9.2 FINAL_TASK 后不回 ACK

测试目的：验证 `FINAL_TASK` ACK 超时和失败处理。

电控发送内容：

- 正常发送 `ARRIVE_DIGIT`
- 收到 `FINAL_TASK` 后故意不回 ACK

视觉期望日志：

```text
[WARN] ACK timeout
[ERROR] ACK failed
```

通过标准：

- 视觉不会假进入 `DONE`；
- `FINAL_TASK` ACK 失败时回 `WAIT_DIGIT_COMMAND` 或保持安全状态。

失败排查方向：

- ACK 是否被电控故意关闭；
- ACK 的 `seq` 是否错误；
- 串口是否在发送后断开。

## 10. port:auto 测试

测试目的：验证 USB 重新插拔后的启动期自动选口能力。

前置条件：

- 配置 `serial.port=auto`

测试步骤：

1. 插上 C 板，记录当前设备号；
2. 启动程序，确认 auto 选口正常；
3. 拔掉 C 板再插回；
4. 观察设备是否从 `/dev/ttyACM0` 变成 `/dev/ttyACM1`；
5. 重启程序；
6. 观察是否自动选中新端口。

期望日志：

```text
[SERIAL] auto selected port=/dev/ttyACM1
```

通过标准：

- 程序重启后能自动选中新的可用端口。

说明：

- 当前不支持运行中自动重连；
- 本测试只验证“启动时自动选择串口”。

## 11. 异常帧测试

测试目的：验证坏帧不会触发业务状态流转。

测试项：

- CRC 错误帧；
- 长度错误帧；
- unknown command，例如 `0x99`。

期望视觉日志：

```text
[WARN] drop frame: crc mismatch
[WARN] drop frame: invalid length
[WARN] unknown command: 0x99
```

通过标准：

- 不触发 `ARRIVE_BEAN / ARRIVE_DIGIT`；
- 不进入业务状态；
- 程序不崩溃。

失败排查方向：

- 电控发包工具是否真的构造了坏帧；
- CRC 是否确实被改坏；
- 长度字段是否和 payload 故意不一致；
- unknown command 是否只触发日志而不被当成有效命令。

## 12. 断联/插拔测试

测试目的：验证断联时的日志表现和当前边界。

测试步骤：

1. 程序运行中连接正常；
2. 运行中拔掉 C 板 USB；
3. 观察 `read/write` 错误日志；
4. 重新插入 C 板；
5. 观察设备是否变成 `/dev/ttyACM1`；
6. 重启程序重新连接。

期望现象：

- 出现串口 `read/write` 错误日志；
- 程序当前不提供运行中自动重连；
- 重新插入后通常需要重启程序。

说明：

- 这不是当前阶段 bug；
- 这是后续通信稳定性阶段任务。

## 13. 全链路闭环测试

完整流程：

`ARRIVE_BEAN`
-> `BEAN_BIND`
-> `ACK`
-> `ARRIVE_DIGIT`
-> `FINAL_TASK`
-> `ACK`
-> `DONE`

通过标准：

- 视觉端进入 `DONE`；
- 电控端正确解析 `BEAN_BIND`；
- 电控端正确解析 `FINAL_TASK`；
- 两个 ACK 都匹配 `seq`；
- 没有 `ERROR` 日志。

建议记录：

- `BEAN_BIND` payload
- `FINAL_TASK` payload
- 两次 ACK 的 `cmd/seq`
- 最终状态是否到 `DONE`

## 14. 失败排查清单

### 14.1 串口打不开

- 检查 USB 是否连接；
- 检查 `/dev/ttyACM*` 或 `/dev/ttyUSB*` 是否存在；
- 检查权限，必要时使用 `sudo`；
- 检查是否被其他程序占用。

### 14.2 没收到 ARRIVE_BEAN

- 检查电控是否真的发包；
- 检查命令字是否为 `0x10`；
- 检查波特率是否一致；
- 检查串口线和 USB 连接。

### 14.3 BEAN_BIND 发出但没 ACK

- 检查电控是否收到 `BEAN_BIND`；
- 检查电控是否正确解析 payload；
- 检查电控 ACK 的 `cmd/seq` 是否匹配；
- 检查是否出现 ACK 超时日志。

### 14.4 ARRIVE_DIGIT 顺序错误

- 检查是否已经完成豆子阶段；
- 检查状态是否仍在 `WAIT_DIGIT_COMMAND` 之前；
- 检查电控是否过早发送 `ARRIVE_DIGIT`；
- 检查现场流程是否与状态机顺序一致。

### 14.5 FINAL_TASK 发出但没 ACK

- 检查电控是否收到 `FINAL_TASK`；
- 检查电控解析是否成功；
- 检查 ACK 是否回了正确 `cmd/seq`；
- 检查发送后是否断联。

### 14.6 port:auto 选错设备

- 检查是否同时连接了多个串口设备；
- 检查当前选中的端口是不是 C 板；
- 暂时可切回显式端口配置；
- 记录现场设备拓扑，避免误插。

### 14.7 插拔后设备名变化

- 重新执行 `ls /dev/ttyACM* /dev/ttyUSB*`；
- 检查是否从 `ttyACM0` 变成 `ttyACM1`；
- 如使用显式端口，修改配置或切到 `auto`；
- 重启程序。

### 14.8 图片模式能跑但 real_robot 不行

- 检查真实相机输入是否正常；
- 检查相机 SDK 或依赖；
- 检查 `input.type` 是否正确；
- 先用 `camera_preview_demo` 单独验证相机。

### 14.9 相机画面黑/暗

- 检查曝光和增益配置；
- 检查镜头遮挡和供电；
- 检查光照环境；
- 先用预览模式观察原始图像。

### 14.10 ROI 不准

- 检查当前机位是否变化；
- 检查 ROI 配置是否与现场一致；
- 检查画面分辨率是否变化；
- 先用相机预览工具重看 ROI。

### 14.11 识别结果 unknown

- 检查画面质量；
- 检查类别映射；
- 检查模型路径和版本；
- 检查是否存在遮挡、反光或目标偏位。

## 15. 现场记录表

| 时间 | 测试项 | 配置文件 | 串口设备 | 结果 | 现象/日志 | 处理方式 | 负责人 |
| --- | --- | --- | --- | --- | --- | --- | --- |
|  |  |  |  |  |  |  |  |
|  |  |  |  |  |  |  |  |
|  |  |  |  |  |  |  |  |

## 16. 当前阶段未覆盖事项

当前暂未覆盖：

- 运行中自动重连；
- `HEARTBEAT`；
- `WATCHDOG`；
- `READY` 状态；
- 多设备身份识别；
- `/dev/serial/by-id` 固定设备；
- 多视角数字融合；
- 实车动作闭环稳定性。
