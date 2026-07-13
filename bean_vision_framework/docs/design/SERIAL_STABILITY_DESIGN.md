# Serial Stability Design

本文档记录 RoboMaster 视觉工程串口通信稳定性方案的设计思路。  
它不是当前已实现功能说明，而是基于现有代码与联调现状提出的后续通信可靠性设计。

当前已经完成：

- USB CDC 基础通信链路
- `SerialPort` 字节收发
- `Protocol` 编解码
- CRC 校验
- ACK 机制
- `SerialCommandSource` 命令桥接
- `ARRIVE_BEAN` 基础命令链路

当前尚未完成：

- 启动握手
- 心跳检测
- 自动恢复
- 通信状态管理

---

## 1. 当前通信架构图

当前主程序中的通信链路：

```text
STM32
  -> USB CDC / ttyACM
  -> SerialPort
  -> Protocol::parsePacket()
  -> SerialCommandSource
  -> main command loop
  -> TaskStateMachine
  -> RecognitionRunner / 旧识别路径
  -> Protocol::make*
  -> SerialPort
  -> STM32
```

当前相关模块职责如下。

### SerialPort

负责：

- 串口打开与关闭
- `115200 8N1`
- 字节收发
- 字节流缓存
- 协议包拆包
- ACK 等待与超时重发

不负责：

- 业务状态
- 自动恢复
- 心跳
- 启动握手
- 通信状态机

### Protocol

负责：

- `HEAD`
- `CMD`
- `LENGTH`
- `SEQ`
- `PAYLOAD`
- `CRC16`
- 协议包编解码

当前协议层已经足够承载后续控制类命令扩展，例如：

- `HELLO`
- `READY`
- `HEARTBEAT`
- `HEARTBEAT_ACK`
- `ERROR`

### SerialCommandSource

负责：

```text
协议包
  -> 业务命令字符串
```

例如：

```text
ARRIVE_BEAN
  -> arrive_bean
```

它不负责：

- 重连
- 心跳
- 通信状态管理
- 串口健康判断

### serial_debug_demo

当前 demo 用于独立验证：

- 串口打开
- 收发数据
- 协议解析
- CRC
- ACK

它当前不是稳定性测试工具，还不具备：

- 握手验证
- 心跳验证
- 自动恢复验证

---

## 2. 当前问题总结

当前工程已经实现“通信可用”，但尚未实现“通信可恢复”。

近期暴露出的风险主要有：

- NUC 偶发无法检测 STM32
- USB CDC 枚举异常
- 重新插拔无法立即恢复
- 双方启动时间不同步

### 已知故障案例

#### USB CDC 问题

现场或调试时可能出现：

- `lsusb` 看不到 STM32
- `/dev/ttyACM0` 不存在
- 插拔后设备不恢复
- 串口设备号变化

可能原因包括：

- USB 枚举失败
- STM32 复位状态异常
- CDC 初始化时序问题
- USB 链路抖动
- 宿主机或 NUC 侧设备状态异常

#### 工业现场风险

比赛现场还可能出现：

- NUC 启动慢
- STM32 启动慢
- USB 设备重新枚举
- 电源波动
- MCU 复位

因此当前工程如果只有“打开串口就直接工作”的策略，鲁棒性是不够的。

---

## 3. 目标架构图

目标不是继续往 `SerialPort` 塞功能，而是在其上层建立通信管理层。

推荐目标结构：

```text
CommunicationManager
  -> SerialPort
  -> Protocol
  -> CommunicationState
  -> HelloReady
  -> Heartbeat
  -> Recovery

CommandSource
  -> TerminalCommandSource / SerialCommandSource

TaskStateMachine
  -> RecognitionRunner
  -> 业务流程
```

通信层目标升级为：

```text
从“通信可用”
升级为“通信可恢复”
```

需要覆盖：

1. NUC 启动
2. STM32 启动
3. 串口打开失败
4. STM32 运行中复位
5. USB 断开
6. 通信恢复

---

## 4. CommunicationState 设计

建议新增独立通信状态机：

```text
DISCONNECTED
CONNECTING
HELLO_SENT
READY
RUNNING
TIMEOUT
RECOVERING
ERROR
```

通信状态机只管理通信，不管理识别和任务。

### DISCONNECTED

进入条件：

- 程序刚启动
- 串口关闭
- 恢复失败后退回初始状态

退出条件：

- 开始尝试打开串口

允许操作：

- 触发 `open()`

### CONNECTING

进入条件：

- 从 `DISCONNECTED` 进入连接尝试
- 从 `RECOVERING` 进入重连

退出条件：

- `SerialPort.open()` 成功，进入 `HELLO_SENT`
- `SerialPort.open()` 失败，退回 `DISCONNECTED` 或等待重试

允许操作：

- 串口打开

### HELLO_SENT

进入条件：

- 串口成功打开
- NUC 发送 `HELLO`

退出条件：

- 收到 STM32 `READY`
- 握手超时

允许操作：

- 等待握手响应

### READY

进入条件：

- 成功收到 `READY`

退出条件：

- 开始进入正常工作

允许操作：

- 初始化业务工作态

### RUNNING

进入条件：

- 握手完成
- 允许业务命令

退出条件：

- 心跳超时
- 读写失败
- 对端复位

允许操作：

- 收发业务包
- 周期心跳

### TIMEOUT

进入条件：

- 心跳丢失
- 长时间无响应

退出条件：

- 进入恢复逻辑

允许操作：

- 标记通信异常

### RECOVERING

进入条件：

- 运行态故障
- 需要重新打开或重新握手

退出条件：

- 重连成功，回到 `CONNECTING` / `HELLO_SENT`
- 多次失败进入 `ERROR`

允许操作：

- 关闭旧串口
- 重连
- 重握手

### ERROR

进入条件：

- 协议严重错误
- 恢复超限失败

退出条件：

- 人工干预
- 或重新初始化

允许操作：

- 只保留诊断与日志

---

## 5. HELLO / READY 设计

### 目标

解决以下问题：

- NUC 已启动但 STM32 还没准备好
- STM32 已启动但 NUC 还没进入工作态
- 双方都在线，但不确认对方是否完成通信初始化

### 推荐启动流程

```text
NUC 启动
  -> SerialPort.open()
  -> 发送 HELLO
  -> STM32 返回 READY
  -> 进入 RUNNING
```

### 设计建议

#### Protocol

应新增控制类 CMD：

- `HELLO`
- `READY`

这属于现有协议的自然扩展，不建议为此单独设计一套新协议。

#### STM32

应增加：

- 接收到 `HELLO` 后返回 `READY`
- 或在初始化完成后主动发送 `READY`

#### 上层逻辑

应维护：

- 当前是否已完成握手
- 未完成握手时是否允许业务命令

### 边界要求

禁止：

- 把 `HELLO/READY` 塞进视觉业务状态机
- 让 `TaskStateMachine` 管握手

握手属于通信层。

---

## 6. Heartbeat 设计

### 目标

区分：

- “串口设备节点还在”

和：

- “对端程序仍在正常运行”

### 推荐命令

- `HEARTBEAT`
- `HEARTBEAT_ACK`

### 推荐策略

#### 周期

建议：

- 100 ms 到 500 ms 周期内选取一个稳定值

比赛系统里更推荐保守值，例如：

- 200 ms

#### 超时阈值

建议：

- 连续 3 到 5 次未收到心跳响应判定超时

例如：

- 200 ms 周期
- 1 s 超时窗口

#### 丢失处理

流程建议：

```text
连续心跳超时
  -> TIMEOUT
  -> RECOVERING
  -> 重连 / 重握手
```

### 作用

心跳不是业务命令，也不是 ACK 替代品。

它的作用是：

- 发现对端程序卡死
- 发现 MCU 复位
- 发现链路虽未物理断开但已不可用

---

## 7. Recovery 设计

恢复设计要分别覆盖不同故障场景。

### 1. 串口打开失败

推荐流程：

```text
open 失败
  -> 等待
  -> 重新 open
```

建议：

- 不要立即退出程序
- 采用有限频率重试

### 2. 运行中断开

推荐流程：

```text
read/write 失败
  -> close
  -> 重新连接
  -> HELLO
```

重点：

- 先恢复通信
- 再恢复业务工作态

### 3. STM32 复位

推荐流程：

```text
检测异常 / 心跳丢失
  -> 判定对端失联
  -> 重新握手
  -> READY 后恢复
```

关键点：

- 不假设对端保留旧状态
- 不直接继续发送业务命令

### 4. NUC 重启

推荐流程：

```text
启动后进入 CONNECTING
  -> HELLO
  -> READY
  -> RUNNING
```

关键点：

- 不假设旧状态存在
- 启动后总是重新同步

---

## 8. 为什么需要 CommunicationManager

推荐新增：

- `CommunicationManager`

用于统一管理：

- `SerialPort`
- `Protocol`
- `CommunicationState`
- `Heartbeat`
- `Recovery`

### 为什么需要

因为当前缺的能力是：

- 启动同步
- 运行时健康检查
- 自动恢复
- 通信状态机

这些能力不属于：

- `SerialPort`
- `SerialCommandSource`

### 为什么不要塞进 SerialPort

`SerialPort` 当前最合理的边界是底层串口 IO：

- open/close
- read/write
- ACK 等待
- 字节流拆包

如果把：

- HELLO
- READY
- HEARTBEAT
- Recovery
- 状态机

都塞进去，会让它从底层 IO 模块膨胀成通信业务模块，边界会被破坏。

### 为什么不要塞进 SerialCommandSource

`SerialCommandSource` 当前职责应保持单一：

```text
协议包
  -> 业务命令字符串
```

它不应承担：

- 串口连接管理
- 握手
- 心跳
- 自动恢复

否则命令桥接层会变成通信管理层。

---

## 9. serial_debug_demo 扩展计划

当前 demo 已能验证：

- 打开串口
- 发送数据
- 接收数据
- CRC
- ACK

未来建议增加三类稳定性测试。

### connect test

目标：

- 验证 `HELLO / READY`

建议能力：

- 打开串口
- 发送 `HELLO`
- 等待 `READY`
- 打印握手结果

### heartbeat test

目标：

- 验证心跳机制

建议能力：

- 周期发送 `HEARTBEAT`
- 统计响应时间
- 模拟超时

### reconnect test

目标：

- 验证断线恢复

建议能力：

- 模拟串口打开失败
- 模拟运行期断开
- 模拟 STM32 复位
- 观察是否能恢复到 `READY`

---

## 10. 与视觉业务的边界

通信层不负责：

- YOLO
- `RecognitionRunner`
- `TaskGenerator`
- `TaskStateMachine`
- 视觉状态推进

业务层只关心：

- 收到命令
- 返回结果

因此边界应保持为：

```text
CommunicationManager
  -> 管连接、握手、心跳、恢复

TaskStateMachine
  -> 管识别与任务流程
```

两者之间只通过“命令可用/不可用”和“结果发送是否成功”交互。

---

## 11. 后续开发顺序

建议顺序如下：

### 第一步

先补通信状态表达：

- `CommunicationState`
- 日志与状态切换设计

### 第二步

补 `HELLO / READY`

目标：

- 解决启动不同步问题

### 第三步

补 `HEARTBEAT / HEARTBEAT_ACK`

目标：

- 发现对端离线
- 发现 MCU 复位

### 第四步

补恢复逻辑

目标：

- 打开失败重试
- 运行中断线恢复
- 重新握手

### 第五步

扩展 `serial_debug_demo`

目标：

- 把稳定性验证从“人工联调”升级成“标准化测试入口”

---

## 12. 结论

当前工程的串口通信已经从“不可用”走到了“可用”，下一步要解决的是“可恢复”。

推荐路线是：

1. 保持 `SerialPort` 底层纯净
2. 在 `Protocol` 中扩展控制类 CMD
3. 新增上层 `CommunicationManager`
4. 用独立的 `CommunicationState` 管理：
   - 启动握手
   - 心跳检测
   - 超时恢复
   - 重新同步

这样做的收益是：

- 不破坏现有串口闭环
- 不污染视觉业务层
- 为比赛现场的 USB CDC 枚举异常、MCU 复位、启动不同步等问题预留可靠恢复路径
