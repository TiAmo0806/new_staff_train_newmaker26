# Serial Communication Phase 1

本文档记录 RoboMaster 视觉工程串口通信基础设施第一阶段的联调成果。  
它不是最终协议冻结文档，重点是说明：

```text
串口基础链路已打通
+ CRC 已对齐
+ ACK 闭环已验证
```

当前内容以双方联调总结中的实际验证结果为准，不仅依据代码推断。

## 1. 阶段目标

本阶段的目标不是直接接完整比赛流程，而是先确认视觉端和电控端的通信基础设施可靠。

阶段目标包括：

- 打通 STM32 C 板 与 NUC 的 USB CDC 串口链路
- 验证 NUC 发送、C 板接收
- 验证 C 板发送、NUC 接收
- 验证双方 CRC 计算一致
- 验证 ACK 请求-响应闭环成立

这一阶段完成后，才能继续推进：

- `SerialCommandSource` 接入主流程
- 状态机由串口事件触发
- 真实视觉结果回传

## 2. 测试环境

### 硬件

- STM32 C 板（F407）
- NUC

### 通信方式

- USB CDC 虚拟串口
- Linux 设备节点：`/dev/ttyACM0`

### 软件

- Ubuntu
- `serial_protocol_demo` / `serial_debug_demo`
- 视觉端复用主工程中的：
  - `Protocol`
  - `SerialPort`

说明：

- 本阶段联调不依赖相机
- 不运行 YOLO
- 不进入完整视觉业务流程

## 3. 已完成内容

### 通信链路

当前已经验证通过的链路：

```text
STM32 C板
  -> USB CDC
  -> /dev/ttyACM0
  -> NUC SerialPort
  -> Protocol 解析
```

### 基础收发

已完成验证：

- NUC 发送，C 板接收
- C 板发送，NUC 接收

双方联调总结的共同结论是：

- 双向通信已打通
- 基础串口联调完成

### CRC

当前双方已经验证一致：

- 算法：`CRC16-MODBUS`
- 初始值：`0xFFFF`
- 多项式：`0xA001`
- 输出异或：`0x0000`
- 字节序：低字节先发
- 校验范围：`HEAD` 到 `PAYLOAD` 最后一个字节，不包含 CRC 本身

这部分不是推测，而是联调已确认结果。

### ACK

当前阶段已经完成 ACK 闭环验证。

已验证模式：

```text
请求帧
  -> 对端接收并解析
  -> 返回 ACK
  -> 发送端确认收到 ACK
```

已确认：

- ACK payload 格式为 `[acked_cmd, acked_seq]`
- ACK 可用于确认对端是否收到指定命令和序号的数据包

## 4. 当前验证命令

### 串口设备检查

```bash
ls /dev/ttyACM*
ls /dev/ttyUSB*
dmesg | grep tty
```

### 权限检查

```bash
groups
```

如果真实串口打不开，应优先检查当前用户是否具备访问对应设备的权限。

### Demo 运行方式

编译：

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
```

帮助：

```bash
cd build
./serial_protocol_demo --help
```

Mock 测试：

```bash
./serial_protocol_demo --mock
```

真实串口监听示例：

```bash
./serial_protocol_demo --port /dev/ttyACM0 --baudrate 115200 --listen
```

发送测试包：

```bash
./serial_protocol_demo --port /dev/ttyACM0 --baudrate 115200 --send-bean-bind
./serial_protocol_demo --port /dev/ttyACM0 --baudrate 115200 --send-final-task
```

说明：

- 当前工程同时生成 `serial_protocol_demo` 和 `serial_debug_demo`
- 两者共用同一套源码
- 阶段联调中优先使用 `serial_protocol_demo` 作为可执行入口

## 5. 当前通信协议状态

### 已确认部分

本阶段已经确认的内容包括：

- 帧结构
- CMD 字段位置
- `LENGTH` 的计算方式
- `SEQ` 的使用方式
- CRC16 方式
- CRC 发送字节序
- ACK payload 结构

当前已验证的统一帧格式是：

```text
HEAD | CMD | LENGTH | SEQ | PAYLOAD | CRC16_L | CRC16_H
```

其中：

- `HEAD = 0xA5`
- 总长度 = `6 + LENGTH`

### 未冻结部分

当前还没有冻结的内容包括：

- 比赛业务命令集合
- 最终 payload 业务语义
- `FINAL_TASK` 的最终业务结构
- 更完整的数字阶段返回格式

因此当前阶段应理解为：

- 基础通信协议骨架已经稳定
- 比赛业务层协议尚未最终冻结

## 6. 当前代码状态

### SerialPort

头文件：

- [include/communication/SerialPort.h](/home/ygk/yolo_competition/bean_vision_framework/include/communication/SerialPort.h:1)

实现：

- [src/communication/SerialPort.cpp](/home/ygk/yolo_competition/bean_vision_framework/src/communication/SerialPort.cpp:1)

当前职责：

- 打开 Linux 串口设备
- 配置 `115200 8N1`
- 发送完整协议帧
- 接收串口字节流
- 流式拆包
- 等待 ACK
- 超时重发

### Protocol

头文件：

- [include/communication/Protocol.h](/home/ygk/yolo_competition/bean_vision_framework/include/communication/Protocol.h:1)

实现：

- [src/communication/Protocol.cpp](/home/ygk/yolo_competition/bean_vision_framework/src/communication/Protocol.cpp:1)

当前职责：

- 定义统一帧格式
- 组包
- 解包
- CRC 校验配合
- 命令字名称映射

当前代码中已定义命令字包括：

- `ARRIVE_BEAN`
- `ARRIVE_DIGIT`
- `RESET`
- `PING`
- `ACK`
- `BEAN_BIND`
- `FINAL_TASK`
- `PONG`

说明：

- “代码已定义”不等于“业务已经全部联调完成”
- 当前阶段应以联调总结中的验证状态为准

### serial_debug_demo

说明文档：

- [tools/serial_debug_demo/README.md](/home/ygk/yolo_competition/bean_vision_framework/tools/serial_debug_demo/README.md:1)

当前职责：

- 独立验证串口链路
- 独立验证协议组包和解析
- 不接相机
- 不跑 YOLO
- 不走状态机主流程

它是当前阶段最重要的联调工具之一。

## 7. 下一阶段任务

在基础通信已经完成的前提下，下一阶段建议按下面顺序推进：

1. `SerialCommandSource` 接入主程序联调链路
2. 用串口命令触发当前状态机
3. 让 `ARRIVE_BEAN` 进入真实视觉主流程
4. 返回真实 `BEAN_BIND`
5. 推进 `ARRIVE_DIGIT` 联调
6. 评估是否扩展 `DIGIT_RESULT`
7. 最后再冻结比赛业务 payload

说明：

- 当前不建议在基础链路刚稳定时就一次性冻结全部比赛协议
- 先让串口事件真正驱动视觉主流程更重要

## 8. 联调经验记录

### USB 设备识别

当前联调基于：

- USB CDC
- `/dev/ttyACM0`

建议每次联调前先检查：

```bash
ls /dev/ttyACM*
```

### 权限

如果设备存在但程序打不开串口，优先检查：

- 当前用户组
- 设备访问权限

### 调试建议

建议严格按这个顺序联调：

1. 先用 `--mock` 验证组包和解析
2. 再用真实串口验证基础收发
3. 再验证 CRC
4. 再验证 ACK
5. 最后才接业务命令

### 当前阶段最重要的经验

- 基础链路先独立验证，不要一开始就接完整视觉主流程
- 串口 demo 必须复用主工程 `Protocol` 和 `SerialPort`
- 只要基础通信未完全稳定，就不要过早冻结比赛业务 payload
