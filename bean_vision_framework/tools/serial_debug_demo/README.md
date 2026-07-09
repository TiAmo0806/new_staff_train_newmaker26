# Serial Protocol Demo

这个 demo 用来单独测试算法端与 C 板之间的串口通信和协议解析。

它不接相机，不跑 YOLO，不走 ROI，也不影响主视觉主程序。

协议字段和编码表见：

- [docs/串口协议说明.md](../../docs/串口协议说明.md)
- [docs/串口协议_核心协议.md](../../docs/串口协议_核心协议.md)
- [docs/串口协议_电控端.md](../../docs/串口协议_电控端.md)
- [docs/串口协议_算法端.md](../../docs/串口协议_算法端.md)

## 编译

在主工程根目录执行：

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
```

会生成：

```text
build/serial_protocol_demo
build/serial_debug_demo
```

说明：

- `serial_protocol_demo` 是推荐使用的名字
- `serial_debug_demo` 是兼容保留的同内容可执行文件

可以直接查看程序内置帮助：

```bash
cd build
./serial_protocol_demo --help
```

## 运行参数

### 1. `--port /dev/ttyACM0`

指定真实串口设备路径。

常见含义：

- `/dev/ttyACM0`
  - 常见于 USB CDC ACM 设备
  - 比如某些 STM32、开发板、串口桥接板

- `/dev/ttyUSB0`
  - 常见于 USB 转串口芯片
  - 比如 CH340、CP210x、FTDI 一类设备

具体用哪个，不是协议决定的，而是 Linux 枚举出来的设备名决定的。

可用下面命令确认：

```bash
ls /dev/ttyACM*
ls /dev/ttyUSB*
dmesg | grep tty
```

### 2. `--baudrate 115200`

指定串口波特率。

当前工程协议默认使用：

```text
115200
```

### 3. `--mock`

不打开真实串口，只在本地打印示例协议包。

适合：

- 没接 C 板时先看协议格式
- 先确认 `TX HEX` / `TX PARSED` 是否正确

### 4. `--send-bean-bind`

主动发送一包示例 `BEAN_BIND`。

适合：

- 测试 C 板能否正确解析第一阶段绑定包

### 5. `--send-final-task`

主动发送一包示例 `FINAL_TASK`。

适合：

- 测试 C 板能否正确解析最终任务包

### 6. `--listen`

进入监听状态，持续读取串口收到的字节并尝试解析。

适合：

- 看 C 板发来的 `ARRIVE_BEAN` / `ARRIVE_DIGIT` / `RESET` / `PING` / `ACK`

## 常见用法

### 1. 本地只看协议格式

```bash
cd build
./serial_protocol_demo --mock
```

含义：

- 不打开任何真实串口
- 默认发送一包 `BEAN_BIND` 和一包 `FINAL_TASK`
- 只打印本地解析结果

### 2. 发一包真实 `BEAN_BIND`

```bash
cd build
./serial_protocol_demo --port /dev/ttyACM0 --baudrate 115200 --send-bean-bind
```

含义：

- 打开 `/dev/ttyACM0`
- 发送一包示例 `BEAN_BIND`
- 发送完即退出

### 3. 发一包真实 `FINAL_TASK`

```bash
cd build
./serial_protocol_demo --port /dev/ttyUSB0 --baudrate 115200 --send-final-task
```

含义：

- 打开 `/dev/ttyUSB0`
- 发送一包示例 `FINAL_TASK`
- 发送完即退出

### 4. 只监听 C 板发来的数据

```bash
cd build
./serial_protocol_demo --port /dev/ttyACM0 --baudrate 115200 --listen
```

含义：

- 不主动发示例业务包
- 持续监听串口
- 打印：
  - `[RX HEX]`
  - `[RX PARSED]`

### 5. 发送示例包后继续监听

```bash
cd build
./serial_protocol_demo --port /dev/ttyACM0 --baudrate 115200 --send-bean-bind --send-final-task --listen
```

含义：

- 先发送 `BEAN_BIND`
- 再发送 `FINAL_TASK`
- 然后持续监听回包和后续命令

### 6. 只写设备和波特率，不写其他参数

```bash
cd build
./serial_protocol_demo --port /dev/ttyACM0 --baudrate 115200
```

默认行为：

- 自动发送一包 `BEAN_BIND`
- 自动发送一包 `FINAL_TASK`
- 如果不是 `--mock`，还会进入监听状态

## 输出说明

发送时：

```text
[TX PARSED]
[TX HEX]
```

接收时：

```text
[RX HEX]
[RX PARSED]
```

说明：

- `TX HEX` / `RX HEX` 是完整十六进制字节流
- `TX PARSED` / `RX PARSED` 是按当前 `Protocol` 解析后的语义结果

## 与主工程关系

当前 demo 直接复用主工程的：

- `Protocol`
- `SerialPort`

因此：

- 串口 demo 和主程序不会维护两套协议
- 主程序协议一旦改动，这个 demo 的行为会同步变化
