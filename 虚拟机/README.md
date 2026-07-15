# 迈德威视相机 + OpenVINO YOLO11 实时目标检测 + 串口通信

## 项目概述

本项目整合 **迈德威视工业相机 SDK**、**OpenVINO 推理框架** 与 **串口通信**，实现：
- 从工业相机实时采集图像
- 加载 YOLO11 OpenVINO IR 模型进行 8 类豆类目标检测
- 按检测物品中心 X 坐标从左到右排序
- 通过串口将排序结果（class_id 序列）发送给电控 MCU（**TX，已实现**）
- 接收 MCU 回传的控制指令（**RX，已实现**）— CTRL 开始/停止采集、单次采集模式
- 支持 MCU 通过串口指令控制推理起停（**软件暂停，已实现**）— `captureEnabled` 标志位控制
- 实时显示 FPS、检测框、排序结果

> 📖 **串口通信完整文档**（包括双向通信设计和电控控制采集方案）请阅读：
> - **[tongxinREADME.md](tongxinREADME.md)** — 串口通信入门 & 代码逐行详解（含双向通信方案）
> - **[receiveME.md](receiveME.md)** — 电控端（MCU）接收协议规范

---

## 项目结构

```
mvs_openvino_demo/
├── main.cpp                       # 主入口 — 串联调用各模块
├── include/
│   ├── camera.hpp                 # 相机模块
│   ├── preprocess.hpp             # 预处理模块
│   ├── detector.hpp               # 推理模块
│   ├── spatial.hpp                # 空间排序模块
│   ├── visualize.hpp              # 绘制模块
│   ├── CRC16.hpp                  # CRC16 校验（串口通信）
│   ├── packet.hpp                 # 帧序列化（TX SendPacket + RX ReceivePacket + McuCommand）
│   └── VirtualSerial.h            # 串口通信模块（TX+RX 双向通信已实现）
├── src/
│   ├── camera.cpp
│   ├── preprocess.cpp
│   ├── detector.cpp
│   ├── spatial.cpp
│   ├── visualize.cpp
│   ├── CRC16.cpp
│   └── VirtualSerial.cpp          # 串口实现（TX+RX 双向通信已实现）
├── model1/                        # OpenVINO IR 模型文件
│   ├── best.xml / best.bin        # YOLO11s 8 类豆类检测
│   ├── best3.xml / best3.bin      # 模型变体
│   └── metadata.yaml
├── tongxinREADME.md               # ★ 串口通信入门 + 双向通信方案 + 电控控制采集方案
├── receiveME.md                   # ★ 电控端（MCU）接收协议规范
├── CMakeLists.txt
├── build.sh
└── README.md
```

---

## 模块架构

```
┌──────────────────────────────────────────────────────────────┐
│  main.cpp（主入口 / 调度者）                                   │
│                                                              │
│  1. 创建 Camera、Detector、VirtualSerial 对象                 │
│  2. 循环：取帧 → 推理 → X坐标排序 → 绘制 → 显示               │
│  3. 排序结果通过串口发送给电控 MCU                             │
│  4. 管理 FPS 统计和按键退出                                   │
└────┬──────────┬──────────┬──────────┬──────────┬─────────────┘
     │          │          │          │          │
     ▼          ▼          ▼          ▼          ▼
┌─────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────────┐
│ Camera  │ │ Detector │ │SpatialSorter│ visualize │ │VirtualSerial │
│ 相机模块 │ │ 推理模块  │ │ X轴排序   │ │ 绘制模块  │ │ 串口通信     │
└────┬────┘ └────┬─────┘ └──────────┘ └──────────┘ └──────────────┘
     │           │
     ▼           ▼
  camera      ┌──────────┐
   SDK        │preprocess│
              │ 预处理    │
              └──────────┘
```

### 各模块职责

| 模块 | 文件 | 职责 | 对外接口 | 状态 |
|------|------|------|---------|------|
| **Camera** | `camera.hpp/cpp` | 相机枚举、初始化、取帧、释放 | `open()`, `read()`, `release()` | ✅ 已实现 |
| **preprocess** | `preprocess.hpp/cpp` | BGR→RGB、Resize(640×640)、归一化、HWC→CHW | `preprocess(frame)`, `blob_to_tensor(blob)` | ✅ 已实现 |
| **Detector** | `detector.hpp/cpp` | 模型加载、推理、后处理、NMS | `load(path)`, `detect(frame)` | ✅ 已实现 |
| **SpatialSorter** | `spatial.hpp/cpp` | 按 X 坐标从左到右排序，格式化输出 | `sortLeftToRight(dets)`, `sortedCenters(dets)`, `formatOrder(sorted)` | ✅ 已实现 |
| **visualize** | `visualize.hpp/cpp` | 画检测框、类别标签、置信度 | `drawDetections(frame, detections)` | ✅ 已实现 |
| **drawCenters** | `spatial.hpp/cpp` | 画中心点、排序序号 | `drawCenters(frame, centers)` | ✅ 已实现 |
| **packet** | `packet.hpp` | TX 帧序列化 + RX 帧反序列化（SendPacket 8 字节 / ReceivePacket 4 字节 / CRC16） | `toVector(packet)`, `parseReceivePacket(raw, packet)` | ✅ TX+RX 已实现 |
| **VirtualSerial** | `VirtualSerial.h/cpp` | 双向串口通信：TX 发送排序结果 + RX 接收 MCU 指令（0x5A + action + CRC16） | `Open()`, `sendDetectionOrder()`, `PollReceive()`, `SetRxCallback()` | ✅ TX+RX 已实现 |

---

## Camera 模块

封装迈德威视 SDK 的底层操作（RAII + 零拷贝）：

```cpp
Camera cam;
cam.open();                    // SDK 初始化 → 枚举 → 配置 → 启动流

cv::Mat frame;
while (cam.read(frame)) {      // 取帧 → Bayer→BGR → 零拷贝 Mat
    // frame 即 BGR Mat
}

cam.release();                 // 析构函数自动调用
```

### 封装细节（调用方无需关心）
- `CameraSdkInit(1)` — SDK 全局初始化
- `CameraEnumerateDevice()` — 枚举设备
- `CameraInit()` — 打开相机
- `CameraGetCapability()` — 读取最大分辨率
- `CameraSetIspOutFormat()` — 黑白/彩色自动适配
- `CameraPlay()` — 启动数据流
- `CameraImageProcess()` — Bayer→BGR
- `CameraReleaseImageBuffer()` — 释放帧缓存（⚠️ 不调用会卡死）

---

## SpatialSorter 模块

按检测物体中心 X 坐标从小到大（画面从左到右）排序：

```cpp
// 排序
auto sorted    = SpatialSorter::sortLeftToRight(detections);
auto centers   = SpatialSorter::sortedCenters(detections);
std::string orderStr = SpatialSorter::formatOrder(sorted);
// orderStr 示例: "1 3 4"  (从左到右依次是 class_id=1,3,4)

// 绘制中心点 + 序号
drawCenters(display, centers);
```

每个物体的中心点标注 `#1`, `#2`... 表示从左到右的排名序号。

---

## VirtualSerial 模块（串口通信）— 数据包详解

> 本节从零开始解释"工控机发给电控的数据包是怎么组装的"。

### 基础概念

#### 什么是"字节"

计算机中数据的最小单位是**字节（Byte）**，1 字节 = 8 个比特（bit），每个比特只能是 0 或 1。所以 1 个字节的取值范围是 `00000000` ~ `11111111`，即十进制的 **0~255**。

人类写代码时用**十六进制**表示一个字节最方便——两位十六进制数恰好表示 256 种状态：

```
十进制 165   =   二进制 10100101   =   十六进制 0xA5
十进制 3     =   二进制 00000011   =   十六进制 0x03
```

代码里的 `0x` 前缀表示这是一个十六进制字面量。`0xA5` 和 `165` 和 `0b10100101` 是**完全相同的数**，只是写法不同。

#### 什么是"帧头"

串口只负责一字节一字节地传数据。MCU 收到一串字节，怎么知道"从哪开始算是一个完整的数据包"？这就需要**帧头**——在每包数据的最前面放一个固定的特殊值作为"起始标志"。

```
连续的字节流:  ... 00 FF 3C A5 03 01 04 5A 1E A5 02 03 07 ...
                                          ↑               ↑
                                    第一个包的帧头      第二个包的帧头
```

MCU 的接收逻辑：**一旦看到 `0xA5`，就知道一个新包开始了**。

#### 为什么选 `0xA5`

| 理由 | 说明 |
|------|------|
| 不是 `0x00` 或 `0xFF` | 这两个值在电路中太常见（空闲电平、干扰），容易误触发 |
| 不是 ASCII 可打印字符 | 串口调试时不会和普通文本混淆 |
| 二进制 `10100101` | 0 和 1 交替出现，电平变化丰富，不易被干扰淹没 |

实际上帧头选什么值不重要，只要**收发双方约定一致**即可。

### 本项目的帧格式

每帧固定 **8 字节**，MCU 每次直接收 8 字节即可：

```
字节位置:  [0]    [1]    [2]   [3]   [4]   [5]   [6]   [7]
         ┌──────┬──────┬──────┬──────┬──────┬──────┬──────┬──────┐
         │ 0xA5 │count │ id0  │ id1  │ id2  │ id3  │CRC_L │CRC_H │
         └──────┴──────┴──────┴──────┴──────┴──────┴──────┴──────┘
         └───────────── 固定 8 字节，MCU 收完即处理 ──────────────┘
```

| 字段 | 长度 | 值 | 含义 |
|------|------|-----|------|
| header | 1 字节 | `0xA5` | 帧头，告诉 MCU"一个新包开始了" |
| count | 1 字节 | 0~4 | 后面跟着几个有效 class_id |
| id0~id3 | 4 字节 | 0~6 或 9 | 按画面从左到右排列的类别 ID，**未用位置填 `9`** |
| CRC16 | 2 字节 | 计算结果 | 对前 6 字节校验（小端序） |

> **类别 ID 对照**：0=黄豆 1=绿豆 2=白芸豆 3=data_1 4=data_2 5=data_3 6=data_4，`9`=无物品哨兵值。

### 代码中逐层组装的过程

以**实际运行输出**为例：

```
[排序] 从左到右: 1
[VirtualSerial] TX order: 1  frame=A5 01 01 09 09 09 XX XX
```

#### 第 1 步：`main.cpp` — 从检测结果提取 class_id

```cpp
// main.cpp 第 87-95 行
auto sorted = SpatialSorter::sortLeftToRight(detections);  // 按 X 坐标排序

if (!sorted.empty()) {
    std::vector<int> classIds;
    for (const auto& d : sorted)
        classIds.push_back(d.class_id);   // 提取每个物体的类别 ID
    serial.sendDetectionOrder(classIds);  // 发出去
}
```

此时 `classIds = [1]`（画面中只检测到 1 个物品，class_id = 1，即绿豆）。

#### 第 2 步：`VirtualSerial.cpp` — 把 class_id 填入帧结构体

```cpp
// VirtualSerial.cpp 第 94-99 行
SendPacket packet;                         // packet.hpp 中定义的结构体
packet.count = static_cast<uint8_t>(n);    // count = 1
for (size_t i = 0; i < n; ++i)
    packet.class_ids[i] = static_cast<uint8_t>(classIds[i]);
    // class_ids[0] = 1
// class_ids[1]~[3] 保持默认值 0，但 toVector() 内会覆盖为 9
```

此时内存中 `SendPacket` 结构体的内容：

```
偏移  字段          值
[0]   header        0xA5    ← 固定帧头
[1]   count         0x01    ← 检测到 1 个物品
[2]   class_ids[0]  0x01    ← 第 1 个物品 class_id=1（绿豆）
[3]   class_ids[1]  0x00    ← 待填充
[4]   class_ids[2]  0x00    ← 待填充
[5]   class_ids[3]  0x00    ← 待填充
```

#### 第 3 步：`packet.hpp` — `toVector()` 填充哨兵值 + 序列化 + 计算 CRC16

```cpp
// packet.hpp toVector() 函数
constexpr size_t DATA_LEN  = 6;    // header(1) + count(1) + class_ids[4](4)
constexpr size_t TOTAL_LEN = 8;    // DATA_LEN + CRC16(2)

// 3a. 未使用的 class_id 位置填充 9
for (int i = packet.count; i < 4; ++i)   // i=1,2,3
    packet.class_ids[i] = 9;

// 内存变为: [A5][01][01][09][09][09]

// 3b. CRC16 对固定 6 字节计算
uint16_t crc = crc16::Get_CRC16_Check_Sum(
    reinterpret_cast<uint8_t *>(&packet), DATA_LEN, 0xFFFF);
// 对 [A5 01 01 09 09 09] 计算 CRC16

// 3c. 拼接：6 字节数据 + 2 字节 CRC = 固定 8 字节
data[0..5] = [A5][01][01][09][09][09]
data[6]    = crc 低字节
data[7]    = crc 高字节
```

#### 第 4 步：`VirtualSerial.cpp` — 写入串口

```cpp
// VirtualSerial.cpp 第 122-128 行
ssize_t written = write(serialFd_, frame.data(), frame.size());
// frame.size() 永远是 8，电控每次收 8 字节
```

### 小端序详解

CRC16 结果是 2 字节（16 位），存入帧时有两种顺序：

```
假设 CRC16 = 0x0708
二进制: 00000111 00001000
        ^^^^^^^^ ^^^^^^^^
        高字节    低字节

大端序（高位在前）： [07] [08]     ← 人类阅读习惯
小端序（低位在前）： [08] [07]     ← 本项目使用

完整帧: A5 01 01 08 07
                └─┬─┘
             CRC 小端序：低字节 0x08 在前
```

**为什么用小端序？** 因为大多数 ARM MCU（STM32 等）的默认字节序就是小端序，和本项目一致，MCU 侧直接把最后 2 字节按 `uint16_t` 读取即可，无需额外转换。

### 完整数据流总结

```
相机帧 → YOLO11 推理 → Detection[]
  │
  ▼
SpatialSorter::sortLeftToRight()
  → classIds = [1]
  │
  ▼
serial.sendDetectionOrder({1})
  │
  ├─ SendPacket{header=0xA5, count=1, class_ids=[1,9,9,9]}
  ├─ toVector(): 填充 9 + CRC16([A5 01 01 09 09 09])
  └─ write(): 发出固定 8 字节 → [A5][01][01][09][09][09][CRC][CRC]
  │
  ▼
终端输出: TX order: 1  frame=A5 01 01 09 09 09 XX XX
  │           ↑                      ↑
  │     人类可读格式              实际十六进制帧（固定 8 字节）
  │
  ▼
C板 MCU 收到 8 字节 → 帧头 0xA5 → count=1 → id0=1 → id1~3=9(忽略) → CRC ✅
```

### 发送日志格式说明

```bash
[VirtualSerial] TX order: 1  frame=A5 01 01 08 07
                           ↑              ↑
                      human-readable   hex frame
                      人类可读的       实际十六进制帧
                      物品序列
```

- `TX order:` 后面的 `1` 是十进制格式，方便人眼看
- `frame=` 后面是实际串口发出的每一个字节的十六进制表示

### VirtualSerial 特性总结

**TX（已实现）：**
- **固定 8 字节帧**：header(0xA5) + count + class_ids[4] + CRC16(2)
- **模拟模式**：`SetSimulated(true)` 无硬件运行，仅打日志
- **自动重连**：断连后扫描 `/dev/ttyACM*`、`/dev/ttyUSB*`，最多等待 5 秒
- **重试机制**：默认重试 3 次，每次间隔 1ms
- **帧日志**：`SetTxLogEnabled(true)` 终端打印 hex 帧，方便调试

**RX（已实现）：**
- **双向通信**：MCU 通过简单 4 字节帧（0x5A + action + CRC16）控制推理起停
- **非阻塞轮询**：`PollReceive()` 在主循环中调用，不引入线程
- **帧回调**：`SetRxCallback()` 设置收到帧时的处理函数
- **电控控制推理**：action=0 停止推理/发送，action=1 恢复推理/发送
- **详细设计文档**：[tongxinREADME.md](tongxinREADME.md) 第五部分（双向通信）和第六部分（电控控制采集）

---

## main.cpp 主入口

```cpp
int main(int argc, char** argv) {
    // 1. 创建 + 初始化
    Camera cam;
    if (!cam.open()) return -1;

    std::string modelPath = (argc > 1) ? argv[1] : "./best.xml";
    Detector detector;
    if (!detector.load(modelPath)) return -1;

    VirtualSerial serial;
    serial.SetTxLogEnabled(true);
    if (!serial.Open()) {
        serial.SetSimulated(true);   // 无串口时模拟运行
    }

    // 双向通信 RX 回调（电控指令 → 控制推理开关）
    bool captureEnabled = true;
    serial.SetRxCallback([&](uint8_t action) {
        captureEnabled = (action == 1);  // 0=停止, 1=开始
    });

    // 2. 主循环
    cv::Mat frame;
    while (cam.read(frame)) {
        serial.PollReceive();    // ★ 非阻塞接收 MCU 指令

        cv::Mat display;
        if (cam.isMono())
            cv::cvtColor(frame, display, cv::COLOR_GRAY2BGR);
        else
            display = frame.clone();

        if (captureEnabled) {    // ★ 电控控制推理开关
            auto detections = detector.detect(frame);                     // 推理
            auto sorted     = SpatialSorter::sortLeftToRight(detections); // 排序
            auto centers    = SpatialSorter::sortedCenters(detections);   // 中心点

            // 串口发送排序结果
            if (!sorted.empty()) {
                std::vector<int> classIds;
                for (const auto& d : sorted) classIds.push_back(d.class_id);
                serial.sendDetectionOrder(classIds);
            }

            // 绘制
            drawDetections(display, sorted);
            drawCenters(display, centers);
        }

        cv::imshow("Real-time Detection (ESC to exit)", display);
        if (cv::waitKey(1) == 27) break;  // ESC 退出
    }

    // 3. 清理
    serial.Close();
    cam.release();
    cv::destroyAllWindows();
    return 0;
}
```

---

## 双向通信（RX）— 已实现 ✅

> 完整内容见 **[tongxinREADME.md](tongxinREADME.md) 第五部分**

以上 main.cpp 只展示了 TX（单向发送）逻辑。双向通信通过以下方式扩展：

### RX 帧格式（MCU → PC，固定 4 字节）

```
字节:  [0]    [1]      [2]      [3]
      0x5A  action  CRC16_LO CRC16_HI
```

TX 帧头 `0xA5`，RX 帧头 `0x5A`，镜像区分方向。

| 字段 | 值 | 含义 |
|------|-----|------|
| action=0 | 停止 | MCU 指令：暂停推理和发送 |
| action=1 | 开始 | MCU 指令：恢复推理和发送 |

### 主循环集成

```cpp
while (cam.read(frame)) {
    serial.PollReceive();    // ★ 非阻塞轮询接收 MCU 指令

    if (captureEnabled) {    // ★ 电控 CTRL 指令控制是否推理
        auto detections = detector.detect(frame);
        // ...排序、发送、绘制...
    }
}
```

### 改动的文件（4 个文件，~170 行新增代码）

| 文件 | 改动 | 行数 |
|------|------|------|
| `include/packet.hpp` | 新增 `ReceivePacket` 结构体 / `parseReceivePacket()`（4 字节帧） | ~45 行 |
| `include/VirtualSerial.h` | 新增 `PollReceive()` / `RxCallback` / RX 缓冲成员 | ~30 行 |
| `src/VirtualSerial.cpp` | 实现 `PollReceive()` / `ParseRxBuffer()` / `HandleValidFrame()` | ~75 行 |
| `main.cpp` | 新增回调 + `PollReceive()` + `captureEnabled` 标志 + 软件暂停逻辑 | ~40 行 |

---

## 电控控制采集 — 软件暂停已实现 ✅

> 完整内容见 **[tongxinREADME.md](tongxinREADME.md) 第六部分**

MCU 通过简单的 4 字节帧控制工控机推理的起停：

```
MCU:  5A 01 XX XX   →  "开始采集，需要检测结果"
MCU:  5A 00 XX XX   →  "停止采集，不需要数据"
```

| action | 含义 | 工控机行为 |
|--------|------|-----------|
| 0 | 停止采集 | 相机继续取帧（画面显示），暂停推理和发送 |
| 1 | 开始采集 | 恢复推理和发送 |

**已实现功能**：`captureEnabled` 标志位控制推理开关，RX 回调中解析 action 字节实时切换。

---

## 依赖与安装

| 依赖 | 用途 | 安装命令 |
|------|------|---------|
| OpenCV 4.x | 图像处理、GUI 显示 | `sudo apt install libopencv-dev` |
| OpenVINO 2024+ | YOLO11 模型推理 | `pip install openvino` |
| 迈德威视 SDK | 工业相机驱动与采集 | 厂商提供 |
| CMake 3.14+ | 编译系统 | `sudo apt install cmake` |
| GCC 9+ (C++17) | 编译器 | `sudo apt install build-essential` |

**运行环境**：Linux（Ubuntu 20.04/22.04/24.04）

### 验证安装

```bash
# OpenCV
pkg-config --modversion opencv4

# OpenVINO
ls ~/.local/lib/python3.10/site-packages/openvino/include/openvino/openvino.hpp

# 迈德威视 SDK
ls /home/hu/mvs/linuxSDK_V2.1.0.49202602041120/include/CameraApi.h
```

---

## 快速开始

### 1. 编译

```bash
cd ~/mvs_openvino_demo
chmod +x build.sh
./build.sh
```

### 2. 准备模型文件

OpenVINO IR 模型文件（`best.xml` + `best.bin`）已包含在 `model1/` 目录下。

编译后需将模型文件复制到 build 目录：

```bash
cp model1/best.xml model1/best.bin build/
```

### 3. 设置库路径

```bash
export LD_LIBRARY_PATH=$HOME/.local/lib/python3.10/site-packages/openvino/libs:$LD_LIBRARY_PATH
```

### 4. 运行

```bash
cd ~/mvs_openvino_demo/build
./mvs_openvino_demo ./best.xml
```

### 5. 退出

按 `ESC` 键退出。

---

## 串口通信配置

本项目通过 Linux 串口（`/dev/ttyACM*` 或 `/dev/ttyUSB*`，115200 8N1）向电控 MCU 发送检测排序结果。

### 1. 查找串口设备

```bash
# 列出所有串口设备
ls -la /dev/ttyACM* /dev/ttyUSB* /dev/ttyS*

# 查看串口设备详细信息（插拔前后对比）
dmesg | grep -i tty | tail -20

# 实时监控 USB 设备插拔
watch -n 1 'ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null'
```

**设备类型说明**：

| 设备名 | 类型 | 说明 |
|--------|------|------|
| `/dev/ttyACM0` | USB 虚拟串口 | STM32/Arduino 等 MCU 直接 USB 连接，最常见 |
| `/dev/ttyUSB0` | USB 转串口 | CH340/CP2102/FT232 等 USB 转串口芯片 |
| `/dev/ttyS0` | 主板原生串口 | 主板自带的 RS-232 接口（物理 DB9 口），不可热插拔 |

> **注意**：虚拟机 / WSL2 环境下，USB 串口设备不会自动出现在客户机中，需要手动将宿主机的 USB 设备**直通（passthrough）**到虚拟机。
>
> - **VMware**：菜单 → 虚拟机 → 可移动设备 → 找到 USB 设备 → 连接
> - **VirtualBox**：设置 → USB → 添加 USB 设备筛选器
> - **WSL2**：在 Windows 宿主机上安装 `usbipd`，然后 `usbipd bind --busid <BUSID>` → `usbipd attach --wsl --busid <BUSID>`

如果你只有 `/dev/ttyS*`（原生串口），且电控 MCU 通过 RS-232 线缆连接，也可以直接使用：

```bash
# 查看哪些原生串口可用
ls -la /dev/ttyS*

# 例如使用 /dev/ttyS0
# 修改 main.cpp 或运行时指定：
# VirtualSerial serial("/dev/ttyS0");
```

### 2. 配置串口权限（一次性）

```bash
# 方法一：将当前用户加入 dialout 组（推荐，重启后生效）
sudo usermod -a -G dialout $USER

# 方法二：临时赋予读写权限（重启后失效）
sudo chmod 666 /dev/ttyACM0
```

### 3. 创建 udev 规则（固定设备名，推荐）

如果设备名经常变化（如 `/dev/ttyACM0` → `/dev/ttyACM1`），可以创建 udev 规则绑定固定名称：

```bash
# 1. 查看设备厂商和产品 ID
lsusb
# 或
udevadm info -a -n /dev/ttyACM0 | grep -E "idVendor|idProduct|serial"

# 2. 创建 udev 规则文件
sudo nano /etc/udev/rules.d/99-mcu-serial.rules
```

写入以下内容（将 `idVendor` 和 `idProduct` 替换为实际值）：

```
# 电控 MCU 串口设备（固定命名为 /dev/mcu_serial）
SUBSYSTEM=="tty", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="5740", SYMLINK+="mcu_serial", MODE="0666"
```

```bash
# 3. 重新加载 udev 规则
sudo udevadm control --reload-rules
sudo udevadm trigger

# 4. 验证
ls -la /dev/mcu_serial
```

创建后，代码中可使用 `/dev/mcu_serial` 代替 `/dev/ttyACM0`：

```cpp
VirtualSerial serial("/dev/mcu_serial");  // 固定名称，不随 USB 口变化
```

### 4. 测试串口通信

```bash
# 方法一：用 screen 监听串口数据
sudo apt install screen
screen /dev/ttyACM0 115200
# 按 Ctrl+A 然后按 K 退出

# 方法二：用 minicom 监听（功能更全）
sudo apt install minicom
minicom -D /dev/ttyACM0 -b 115200

# 方法三：用 cat 查看原始数据
cat /dev/ttyACM0
# 或查看十六进制数据
cat /dev/ttyACM0 | xxd

# 方法四：发送测试数据到串口（用于验证发送功能）
echo -ne '\xA5\x03\x01\x02\x03' > /dev/ttyACM0
```

### 5. 串口通信调试参数

代码中可通过以下方式调整串口行为：

```cpp
VirtualSerial serial("/dev/ttyACM0");

serial.SetTxLogEnabled(true);    // 终端打印发送的十六进制帧数据
serial.SetAutoReconnect(true);   // 串口断开后自动扫描重连（默认开启）
serial.SetSimulated(true);       // 模拟模式：不操作真实串口，仅打印日志
```

### 6. 无串口硬件时运行（模拟模式）

如果当前环境没有串口设备，程序会自动降级为模拟模式运行：

```bash
# 直接运行即可，串口打开失败会自动启用模拟模式
./mvs_openvino_demo ./best.xml

# 输出示例：
# [WARN] 串口打开失败，将以模拟模式运行
# [VirtualSerial] Simulated TX order: 0 3 1
```

也可以在代码中手动启用：

```cpp
serial.SetSimulated(true);  // 强制模拟模式，跳过真实串口操作
```

### 7. 排查串口通信问题

| 现象 | 排查步骤 |
|------|---------|
| 找不到 `/dev/ttyACM*` | `dmesg \| grep tty` 查看内核日志；重新插拔 USB 线；VM/WSL 需直通 USB 设备 |
| 只有 `/dev/ttyS*` 无 USB 串口 | 虚拟机需在宿主机上将 USB 设备直通；物理机检查 USB 线是否插好、MCU 是否上电 |
| Permission denied | `sudo chmod 666 /dev/ttyACM0` 或 `sudo usermod -aG dialout $USER` |
| 发送数据 MCU 没反应 | 检查波特率是否一致（默认 115200）；用示波器或逻辑分析仪抓 TX 引脚 |
| 程序启动后闪退 | 检查串口是否被其他程序占用：`lsof /dev/ttyACM0` |
| 运行中串口断开 | 代码已内置自动重连（`TryReconnect()`），最多等待 5 秒 |

---

## 常见问题

### 编译阶段

| 问题 | 解决方案 |
|------|---------|
| `找不到 openvino.hpp` | `pip install openvino` |
| `找不到 -lopenvino` | 检查 `build/openvino_links/` 符号链接 |
| OpenCV 找不到 | `sudo apt install libopencv-dev` |
| 迈德威视 SDK 找不到 | 修改 `CMakeLists.txt` 中 `MV_SDK_ROOT` |

### 运行阶段

| 问题 | 解决方案 |
|------|---------|
| 模型文件打不开 | `.xml` 和 `.bin` 需在同一目录（模型文件位于 `model1/` 目录下） |
| `.so` 找不到 | 设置 `LD_LIBRARY_PATH` |
| 检测不到相机 | `lsusb` 检查；可能需要 sudo 或 udev 规则 |
| 串口打不开 | 检查 `/dev/ttyACM*`；可用 `SetSimulated(true)` 跳过 |
| 串口写失败 + 重连失败 | **虚拟机常见**：USB 设备未直通到虚拟机。VMware: 菜单→虚拟机→可移动设备→连接。检查: `ls /dev/ttyACM*` |
| 推理速度慢 | `"AUTO"` 改为 `"GPU"`（需 Intel GPU） |
| 画面卡死 | 检查是否忘记 `CameraReleaseImageBuffer` |
| 检测框不准 | 调整 `detector.confThreshold`（默认 0.4）和 `detector.nmsThreshold`（默认 0.5） |
| python3.10 路径不存在 | `find ~/.local -name "openvino.hpp"` 查找实际路径 |

---

## 开发对话中的踩坑记录

### 坑 1：`set_input_tensor()` API 类型不匹配

`blob_to_tensor` 返回 `std::shared_ptr<ov::Tensor>` 但 API 需要 `const ov::Tensor&`。改为值返回，利用 C++ 移动语义。

### 坑 2：`get_any_name()` 在无名节点上抛异常

YOLO11 导出 IR 模型输出节点可能无名称。先用 `get_names()` 检查是否为空。

### 坑 3：链接器找不到 `-lopenvino`

pip 安装的库带版本后缀（`libopenvino.so.2621`）。CMake 中通过 `ln -sf` 创建符号链接解决。

#### 什么是符号链接

符号链接（Symbolic Link，symlink）就是一个**指向另一个文件的"快捷方式"**，类比 Windows 的 `.lnk` 快捷方式。

OpenVINO 通过 pip 安装后，真实的库文件名带版本号：

```bash
ls ~/.local/lib/python3.10/site-packages/openvino/libs/
# libopenvino.so.2621          ← 真正的文件（带版本后缀）
# libopenvino_c.so.2621
# libopenvino_ir_frontend.so.2621
```

但编译器和链接器按约定只找不带版本号的名字：

```
g++ ... -lopenvino          # 告诉链接器："找 libopenvino.so"
                             # 但磁盘上只有 libopenvino.so.2621
                             # → 找不到！报错
```

**解决方案**：创建一个符号链接，让不带版本号的名字指向真实的文件：

```bash
ln -sf libopenvino.so.2621  libopenvino.so
#      ↑ 真正的文件           ↑ 符号链接（快捷方式）
```

结果：

```
libopenvino.so  →  libopenvino.so.2621    # 编译器找到 .so → 跟随链接 → 加载真实文件
```

本项目 `CMakeLists.txt` 中的处理（自动完成，无需手动操作）：

```cmake
# 在 build 目录下创建符号链接，让 -lopenvino 能被 ld 找到
file(GLOB OPENVINO_SO_FILES "${OPENVINO_LIB_DIR}/libopenvino.so.*")
execute_process(COMMAND ln -sf ${OPENVINO_SO_FILE} ${OPENVINO_LINK_DIR}/libopenvino.so)
```

#### 硬链接 vs 符号链接

| | 符号链接 (symlink) | 硬链接 (hard link) |
|---|---|---|
| 本质 | 路径指针，指向另一个**文件名** | 同一个文件实体（inode）的两个**名字** |
| 跨分区 | ✅ 可以 | ❌ 不可以（必须同一文件系统） |
| 原文件删除后 | 链接变"死链接"（broken link） | 数据仍然存在（引用计数 > 0） |
| 创建命令 | `ln -s 源文件 链接名` | `ln 源文件 链接名` |
| 查看 | `ls -l` 输出中显示 `→` 箭头 | 和普通文件完全一样，无法区分 |

#### 常用命令

```bash
# 创建符号链接
ln -s /path/to/real/file.so.2621  link_name.so

# 查看链接指向哪里
ls -l link_name.so
# 输出: link_name.so -> /path/to/real/file.so.2621

# 删除符号链接（不影响原文件）
rm link_name.so

# 查找所有符号链接
find . -type l

# 查找死链接（原文件已被删除）
find . -xtype l
```

### 坑 4：运行时找不到 `.so` 文件

需设置 `LD_LIBRARY_PATH` 指向 `~/.local/lib/python3.10/site-packages/openvino/libs`。
