## 1. Demo简介

该 Demo 用于独立测试：
```

STM32 C板

↓

USB CDC虚拟串口

↓

NUC视觉端 SerialPort

↓

Protocol解析

```

用于验证：

- 串口链路是否正常
- 数据帧是否正确
- CRC校验是否一致
- 双向通信是否成功


注意：

该 Demo：

- 不连接相机
- 不运行YOLO
- 不执行ROI解析
- 不进入视觉状态机主流程

不会影响主视觉程序。

---

## 2. 协议说明

本 Demo 直接复用主工程：

- Protocol
- SerialPort


不会维护独立协议。


协议文档：
```

docs/

├── 串口协议说明.md  
├── 串口协议_核心协议.md  
├── 串口协议_电控端.md  
└── 串口协议_算法端.md

````

---

# 3. 当前联调支持范围

## STM32 → NUC

电控发送：

|CMD|名称|Payload|
|-|-|-|
|0x10|ARRIVE_BEAN|0 byte|
|0x11|ARRIVE_DIGIT|0 byte|
|0x12|RESET|0 byte|
|0x13|PING|0 byte|


---

## NUC → STM32

视觉发送：

|CMD|名称|Payload|
|-|-|-|
|0x02|FINAL_TASK|11 byte|
|0x04|BEAN_BIND|10 byte|
|0x05|PONG|0 byte|
|0x14|ACK|2 byte|


---

# 4. 编译

在主工程目录：

```bash
cmake -S . -B build

cmake --build build -j$(nproc)
````

生成：

```
build/

├── serial_protocol_demo

└── serial_debug_demo
```

说明：

- serial_protocol_demo
    - 推荐使用
- serial_debug_demo
    - 兼容旧名称

查看帮助：

```
cd build

./serial_protocol_demo --help
```

---

# 5. 参数说明

## 5.1 指定串口

```
--port /dev/ttyACM0
```

常见：

```
/dev/ttyACM0

USB CDC ACM设备

例如 STM32 USB虚拟串口
```

或者：

```
/dev/ttyUSB0

USB转串口设备
```

查看设备：

```
ls /dev/ttyACM*

ls /dev/ttyUSB*

dmesg | grep tty
```

---

## 5.2 波特率

```
--baudrate 115200
```

当前协议：

```
115200 8N1
```

---

## 5.3 mock模式

```
--mock
```

作用：

不打开真实串口。

仅测试：

- Protocol组包
- Protocol解析

适合：

没有连接C板时测试。

---

## 5.4 发送测试包

### BEAN_BIND

```
--send-bean-bind
```

发送：

```
CMD = 0x04
```

用于测试：

NUC → STM32

---

### FINAL_TASK

```
--send-final-task
```

发送：

```
CMD = 0x02
```

用于测试：

NUC → STM32

---

## 5.5 监听模式

```
--listen
```

持续监听串口。

输出：

```
[RX HEX]

[RX PARSED]
```

推荐真实联调时使用。

---

# 6. 推荐第一次联调流程

## Step 1

启动NUC监听：

```
cd build

./serial_protocol_demo \
--port /dev/ttyACM0 \
--baudrate 115200 \
--listen
```

---

## Step 2

STM32上电。

当前C板测试程序：

每2秒发送：

```
ARRIVE_BEAN
```

NUC预期：

```
RX cmd=0x10

ARRIVE_BEAN
```

---

## Step 3

NUC回复ACK。

ACK格式：

```
CMD = 0x14

payload:

acked_cmd

acked_seq
```

例如：

收到：

```
ARRIVE_BEAN

seq=5
```

回复：

```
ACK

payload:

10 05
```

---

## Step 4

测试视觉发送结果包。

发送：

```
./serial_protocol_demo \
--port /dev/ttyACM0 \
--baudrate 115200 \
--send-bean-bind
```

STM32预期：

收到：

```
BEAN_BIND
```

回复：

```
ACK
```

---

## Step 5

测试最终任务：

```
./serial_protocol_demo \
--port /dev/ttyACM0 \
--baudrate 115200 \
--send-final-task
```

STM32预期：

收到：

```
FINAL_TASK
```

回复：

```
ACK
```

---

# 7. 常见问题

## 7.1 找不到串口

检查：

```
ls /dev/ttyACM*
```

如果权限不足：

```
sudo usermod -aG dialout $USER
```

重新登录。

---

## 7.2 CRC错误

优先检查：

1. CRC算法

必须：

```
CRC16-MODBUS
```

2. CRC范围

必须包含：

```
HEAD

CMD

LENGTH

SEQ

PAYLOAD
```

不包含：

```
CRC自身
```

3. 字节序

发送：

```
crc_low

crc_high
```

---

# 8. 与主视觉程序关系

主程序：

```
Camera

↓

Detector

↓

ROI

↓

TaskGenerator

↓

Protocol

↓

SerialPort
```

Demo：

```
Protocol

↓

SerialPort

↓

STM32
```

二者共享：

- Protocol
- SerialPort

因此：

协议修改后，

Demo和主程序同步变化。

---

# 9. 当前Demo目标

完成：

```
STM32

<---->

NUC
```

可靠通信。

验证：

- USB CDC
- 数据帧
- CRC
- ACK
- 业务包解析

完成后再接入：

- YOLO识别
- 状态机
- 比赛流程

这个版本更符合你现在的阶段：**先证明“机器人两块板能说话”，再让视觉业务接管通信。**  
后续如果电控再加入真实遥控/电机逻辑，只需要更新“联调流程”部分，不需要重写整个 README。
