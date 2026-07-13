# 视觉串口通信协议说明

本文档面向 STM32 电控工程师，描述 RoboMaster 视觉端当前串口通信协议实现。

本文档依据当前代码实现整理，主要对应：

- `include/communication/Protocol.h`
- `src/communication/Protocol.cpp`
- `include/communication/SerialPort.h`
- `src/communication/SerialPort.cpp`
- `src/command/SerialCommandSource.cpp`
- `src/task/TaskStateMachine.cpp`
- `config/real_robot.yaml`

不涉及视觉内部算法细节，只说明：

- 收到什么消息
- 发送什么消息
- 数据如何解析
- ACK 与时序如何工作

## 1. 串口基础参数

当前代码默认真实联调配置见 `config/real_robot.yaml`。

Port:

- `/dev/ttyACM0`

Baudrate:

- `115200`

Data bits:

- `8`

Stop bits:

- `1`

Parity:

- `None`

Flow control:

- `None`

Linux 权限需求：

- 视觉程序运行用户需要有串口设备访问权限
- 常见要求是用户属于 `dialout` 组
- 当前默认设备名是 `/dev/ttyACM0`

补充说明：

- 代码使用 `O_RDWR | O_NOCTTY | O_SYNC` 打开串口
- 接收采用非阻塞样式轮询读取
- `VMIN = 0`
- `VTIME = 5`
- 若实际 STM32 侧不是 USB CDC，而是 UART 转 USB，请与视觉侧确认设备名是否仍为 `/dev/ttyACM0`

## 2. 数据帧格式

当前协议统一帧格式为：

```text
HEAD | CMD | LENGTH | SEQ | PAYLOAD | CRC
```

完整字节布局：

```text
0xA5 | cmd | length | seq | payload... | crc_low | crc_high
```

字段说明：

| 字段 | 长度 | 含义 |
| - | - | - |
| HEAD | 1 byte | 固定帧头，当前固定为 `0xA5` |
| CMD | 1 byte | 命令字 |
| LENGTH | 1 byte | payload 长度，单位字节 |
| SEQ | 1 byte | 包序号，发送端每发一包自增 |
| PAYLOAD | `LENGTH` bytes | 业务数据 |
| CRC | 2 bytes | CRC16 校验，低字节在前，高字节在后 |

包总长度计算：

```text
total_length = 1 + 1 + 1 + 1 + LENGTH + 2
             = 4 + LENGTH + 2
```

### 2.1 CRC 说明

CRC 算法：

- `CRC16-MODBUS`

初始值：

- `0xFFFF`

多项式：

- `0xA001`

CRC 计算范围：

- 包含 `HEAD`
- 包含 `CMD`
- 包含 `LENGTH`
- 包含 `SEQ`
- 包含 `PAYLOAD`
- 不包含最终附加的两个 CRC 字节

也就是：

```text
CRC = CRC16_MODBUS(HEAD | CMD | LENGTH | SEQ | PAYLOAD)
```

CRC 发送顺序：

```text
crc_low
crc_high
```

示例：

```text
A5 10 00 01 xx xx
```

其中最后两个字节是对前四个字节 `A5 10 00 01` 计算得到的 CRC16-MODBUS，且低字节先发。

## 3. 命令字定义

当前代码中定义的命令字如下：

| CMD | 名称 | 方向 | 触发条件 | payload |
| - | - | - | - | - |
| `0x01` | `VISION` | NUC -> 外部 | 调试保留，当前主流程默认不发送 | 9 bytes |
| `0x02` | `FINAL_TASK` | NUC -> STM32 | 数字识别完成且任务生成成功后发送 | 11 bytes |
| `0x03` | `ERROR` | NUC -> STM32 | 当前接口已实现，主流程暂未默认使用 | 1 byte |
| `0x04` | `BEAN_BIND` | NUC -> STM32 | 豆子识别完成后发送 | 10 bytes |
| `0x05` | `PONG` | NUC -> STM32 | 收到 `PING` 后立即回复 | 0 byte |
| `0x10` | `ARRIVE_BEAN` | STM32 -> NUC | 车辆到达豆子识别区域 | 0 byte |
| `0x11` | `ARRIVE_DIGIT` | STM32 -> NUC | 车辆到达数字识别区域 | 0 byte |
| `0x12` | `RESET` | STM32 -> NUC | 请求视觉清空当前任务状态 | 0 byte |
| `0x13` | `PING` | STM32 -> NUC | 链路探活 | 0 byte |
| `0x14` | `ACK` | 双向 | 确认某一帧已收到 | 2 bytes |

## 4. Payload 详细定义

以下按字节展开说明每个带 payload 的命令。

### 4.1 `VISION` (`0x01`)

方向：

- `NUC -> 外部`

用途：

- 调试保留包
- 当前主流程默认不发送

payload 长度：

- `9`

格式：

```text
byte0  success
byte1  p1_class_id
byte2  p2_class_id
byte3  p3_class_id
byte4  l4_class_id
byte5  l5_class_id
byte6  l6_class_id
byte7  l7_class_id
byte8  l8_class_id
```

字段说明：

- `success`
  - 类型：`uint8_t`
  - 编码：`1` 表示成功，`0` 表示失败
- `p1_class_id ~ l8_class_id`
  - 类型：`uint8_t`
  - 编码：直接使用视觉内部检测类别 id
  - 无效位置填 `255`

### 4.2 `BEAN_BIND` (`0x04`)

方向：

- `NUC -> STM32`

用途：

- 豆子识别阶段结果
- 表示 `P1/P2/P3` 上识别到的豆子种类，以及每个豆子对应的目标数字

payload 长度：

- 固定 `10`

格式：

```text
byte0  count

byte1  pickup_1
byte2  bean_1
byte3  target_digit_1

byte4  pickup_2
byte5  bean_2
byte6  target_digit_2

byte7  pickup_3
byte8  bean_3
byte9  target_digit_3
```

字段说明：

- `count`
  - 类型：`uint8_t`
  - 含义：有效绑定数量
  - 取值范围：`0~3`

- `pickup_i`
  - 类型：`uint8_t`
  - 编码：取货位编码
  - `P1=1, P2=2, P3=3`
  - 无效占位填 `0`

- `bean_i`
  - 类型：`uint8_t`
  - 编码：豆子类型编码
  - `soybean=0, mung_bean=1, white_kidney_bean=2`
  - 无效占位填 `255`

- `target_digit_i`
  - 类型：`uint8_t`
  - 编码：目标数字编码
  - `digit_1=1, digit_2=2, digit_3=3, digit_4=4, digit_5=5`
  - 无效占位填 `0`

无效项占位规则：

```text
pickup = 0
bean = 255
target_digit = 0
```

### 4.3 `FINAL_TASK` (`0x02`)

方向：

- `NUC -> STM32`

用途：

- 最终任务结果
- 数字识别完成后发送

payload 长度：

- 固定 `11`

格式：

```text
byte0  success
byte1  task_count

byte2  from_1
byte3  to_1
byte4  bean_1

byte5  from_2
byte6  to_2
byte7  bean_2

byte8  from_3
byte9  to_3
byte10 bean_3
```

字段说明：

- `success`
  - 类型：`uint8_t`
  - 编码：`1` 表示任务生成成功，`0` 表示失败

- `task_count`
  - 类型：`uint8_t`
  - 含义：有效任务数量
  - 当前最大为 `3`

- `from_i`
  - 类型：`uint8_t`
  - 编码：取货位
  - `P1=1, P2=2, P3=3`
  - 无效占位填 `0`

- `to_i`
  - 类型：`uint8_t`
  - 编码：放置位
  - `L4=4, L5=5, L6=6, L7=7, L8=8`
  - 无效占位填 `0`

- `bean_i`
  - 类型：`uint8_t`
  - 编码：豆子类型
  - `soybean=0, mung_bean=1, white_kidney_bean=2`
  - 无效占位填 `0`

空任务占位规则：

```text
from = 0
to = 0
bean = 0
```

### 4.4 `ERROR` (`0x03`)

方向：

- `NUC -> STM32`

用途：

- 错误上报
- 当前协议接口已实现，主流程默认未自动使用

payload 长度：

- `1`

格式：

```text
byte0 error_code
```

字段说明：

- `error_code`
  - 类型：`uint8_t`
  - 含义：错误码
  - 具体枚举当前未在代码中进一步定义，需要联调时与电控确认

### 4.5 `ACK` (`0x14`)

方向：

- 双向

用途：

- 确认对方已收到某一业务包

payload 长度：

- `2`

格式：

```text
byte0 acked_cmd
byte1 acked_seq
```

字段说明：

- `acked_cmd`
  - 类型：`uint8_t`
  - 含义：被确认的命令字

- `acked_seq`
  - 类型：`uint8_t`
  - 含义：被确认的包序号

匹配规则：

- `cmd == ACK`
- `payload[0] == expected_cmd`
- `payload[1] == expected_seq`

### 4.6 `PING` (`0x13`) / `PONG` (`0x05`)

payload 长度：

- `0`

无 payload。

### 4.7 `ARRIVE_BEAN` (`0x10`)

方向：

- `STM32 -> NUC`

payload 长度：

- `0`

无 payload。

### 4.8 `ARRIVE_DIGIT` (`0x11`)

方向：

- `STM32 -> NUC`

payload 长度：

- `0`

无 payload。

### 4.9 `RESET` (`0x12`)

方向：

- `STM32 -> NUC`

payload 长度：

- `0`

无 payload。

## 5. 编码表整理

### 5.1 豆子类型编码

```text
0   soybean
1   mung_bean
2   white_kidney_bean
255 invalid   (仅 BEAN_BIND 无效占位时使用)
```

### 5.2 数字类型编码

```text
1 digit_1
2 digit_2
3 digit_3
4 digit_4
5 digit_5
0 invalid / empty
```

### 5.3 取货位编码

```text
P1 = 1
P2 = 2
P3 = 3
```

### 5.4 放置位编码

```text
L4 = 4
L5 = 5
L6 = 6
L7 = 7
L8 = 8
```

### 5.5 成功标志编码

```text
1 success
0 fail
```

### 5.6 无效占位规则

`BEAN_BIND` 无效项：

```text
pickup = 0
bean = 255
target_digit = 0
```

`FINAL_TASK` 空任务项：

```text
from = 0
to = 0
bean = 0
```

`VISION` 中无识别位置：

```text
class_id = 255
```

## 6. ACK 机制

### 6.1 视觉端发送后是否等待 ACK

当前真实联调配置中：

- `ack_timeout_ms = 100`
- `max_resend = 3`

含义：

- 视觉端发送业务包后，会等待对端 `ACK`
- 超时则重发
- 最多重发 `3` 次
- 总发送尝试次数为 `max_resend + 1 = 4`

### 6.2 哪些包默认等待 ACK

理论上，除以下两类外，其余发送包都会进入 ACK 等待流程：

- `ACK`
- `PONG`

也就是说：

- `BEAN_BIND` 会等 ACK
- `FINAL_TASK` 会等 ACK
- `ERROR` 会等 ACK
- `VISION` 如果后续启用，也会等 ACK

### 6.3 哪些入站命令视觉端会立即回复 ACK

视觉端当前会对以下 STM32 命令立即回复 ACK：

- `ARRIVE_BEAN`
- `ARRIVE_DIGIT`
- `RESET`

回复规则：

```text
ACK payload[0] = 收到的 cmd
ACK payload[1] = 收到的 seq
```

### 6.4 `PING` 特殊规则

收到 `PING` 后：

- 不回 `ACK`
- 直接回 `PONG`

### 6.5 ACK 示例

若 STM32 发送：

```text
CMD = 0x10
SEQ = 0x23
```

视觉端正确收到后会回：

```text
CMD = 0x14
PAYLOAD = 0x10 0x23
```

## 7. 通信流程

以下描述当前真实代码行为。

### 7.1 豆子识别流程

```text
STM32
发送 ARRIVE_BEAN
↓
NUC
校验帧头 / 长度 / CRC
↓
NUC
发送 ACK(acked_cmd=ARRIVE_BEAN, acked_seq=对方seq)
↓
NUC
进入豆子识别流程
↓
NUC
识别完成后发送 BEAN_BIND
↓
NUC
等待 STM32 ACK
```

业务含义：

- `ARRIVE_BEAN` 只表示“现在可以做豆子识别了”
- 真正的识别结果由 `BEAN_BIND` 返回

### 7.2 数字识别流程

```text
STM32
发送 ARRIVE_DIGIT
↓
NUC
校验帧头 / 长度 / CRC
↓
NUC
发送 ACK(acked_cmd=ARRIVE_DIGIT, acked_seq=对方seq)
↓
NUC
进入数字识别流程
↓
NUC
识别并生成最终任务
↓
NUC
发送 FINAL_TASK
↓
NUC
等待 STM32 ACK
```

### 7.3 RESET 流程

```text
STM32
发送 RESET
↓
NUC
发送 ACK(acked_cmd=RESET, acked_seq=对方seq)
↓
NUC
清空当前任务状态
↓
NUC
回到等待豆子阶段
```

### 7.4 PING 探活流程

```text
STM32
发送 PING
↓
NUC
发送 PONG
```

### 7.5 视觉状态机与串口事件关系

当前视觉端状态机主要对电控呈现三个关键阶段：

```text
WAIT_BEAN_COMMAND
WAIT_DIGIT_COMMAND
DONE
```

对应关系：

- `ARRIVE_BEAN`
  - 允许触发条件：视觉端处于 `WAIT_BEAN_COMMAND`
  - 结果：进入豆子识别

- `BEAN_BIND` 发送完成后
  - 视觉端转到 `WAIT_DIGIT_COMMAND`

- `ARRIVE_DIGIT`
  - 允许触发条件：视觉端处于 `WAIT_DIGIT_COMMAND`
  - 结果：进入数字识别

- `FINAL_TASK` 发送完成后
  - 视觉端转到 `DONE`

- `RESET`
  - 任意时刻可用于要求视觉端重置回初始状态

STM32 侧建议按照如下逻辑驱动：

1. 先发 `ARRIVE_BEAN`
2. 收到 `ACK`
3. 等待 `BEAN_BIND`
4. 给 `BEAN_BIND` 回 ACK
5. 再发 `ARRIVE_DIGIT`
6. 收到 `ACK`
7. 等待 `FINAL_TASK`
8. 给 `FINAL_TASK` 回 ACK

## 8. 与电控必须确认事项

以下事项需要视觉与电控在真实联调前明确：

1. 当前链路是否确定为 USB CDC  
当前视觉默认设备是 `/dev/ttyACM0`，这通常对应 USB CDC ACM 设备。

2. STM32 侧是否完全使用相同 CRC 算法  
必须确认是 `CRC16-MODBUS`，初始值 `0xFFFF`，多项式 `0xA001`。

3. CRC 计算范围是否完全一致  
必须确认 CRC 覆盖：
- HEAD
- CMD
- LENGTH
- SEQ
- PAYLOAD

4. CRC 字节序是否一致  
当前视觉端发送顺序是：
- `crc_low`
- `crc_high`

5. ACK 超时时间是否接受  
当前视觉端默认等待 `100 ms`。

6. 是否采用相同重发策略  
当前视觉端默认最多重发 `3` 次。

7. `SEQ` 溢出处理是否接受  
当前 `SEQ` 是 `uint8_t`，发送端自增，溢出后会从 `0xFF` 回绕到 `0x00`。

8. STM32 是否对所有业务包回 ACK  
至少需要确认是否对以下包回 ACK：
- `BEAN_BIND`
- `FINAL_TASK`
- `ERROR`（若后续启用）

9. `PING` 是否只回 `PONG`，不回 `ACK`  
当前视觉实现是收到 `PING` 后只回 `PONG`。

10. 是否需要错误码枚举表  
`ERROR` 包已预留，但当前错误码未在协议中进一步枚举。

11. `VISION` 调试包是否需要使用  
当前主流程默认不发 `VISION`，若电控需要调试使用，需要双方确认。

12. 是否接受固定长度 payload 设计  
`BEAN_BIND` 固定 10 字节，`FINAL_TASK` 固定 11 字节，空位采用占位值，不是变长数组。

## 9. 电控侧最小实现建议

STM32 侧最少需要实现：

1. 按统一帧格式封包
2. 按 `CRC16-MODBUS` 校验
3. 识别以下入站包：
- `BEAN_BIND`
- `FINAL_TASK`
- `PONG`
- `ACK`
- 可选 `ERROR`
4. 能发送以下出站包：
- `ARRIVE_BEAN`
- `ARRIVE_DIGIT`
- `RESET`
- `PING`
- `ACK`
5. 对 `BEAN_BIND` 和 `FINAL_TASK` 正确回 ACK

## 10. 当前默认联调配置

视觉端 `real_robot` 当前默认串口配置：

```text
port = /dev/ttyACM0
baudrate = 115200
ack_timeout_ms = 100
max_resend = 3
```

如需修改端口名或波特率，应由视觉侧配置文件调整并重新运行。
