# C 板 ↔ NUC 视觉串口通信 联调总结文档

> 版本：v1.0 基础版本（payload 简化版）
>
> 更新日期：2026-07-11
>
> 状态：**✅ 双向通信已打通，基础联调完成**

---

## 一、通信架构总览

```
┌──────────────┐                    ┌──────────────┐
│   NUC 视觉端  │                    │   C 板 F407   │
│              │     USB CDC        │              │
│  serial_     │◄──────────────────►│  usbd_cdc_if │
│  protocol_   │    /dev/ttyACM0    │       ↓      │
│  demo        │                    │  proto_poll  │
│              │                    │       ↓      │
│              │                    │  freertos.c  │
│              │                    │   defaultTask│
└──────────────┘                    └──────────────┘

同时存在第二条链路：

┌──────────────┐         2.4G          ┌──────────────┐
│ 遥控器 C8T6  │◄─────────────────────►│   C 板 F407   │
│ NRF24L01 TX  │   SPI1 / CH40 / 2Mbps│ NRF24L01 RX   │
│  Packet_t    │                      │   nrfTask     │
└──────────────┘                       └──────────────┘
```

---

## 二、协议帧格式（已验证通过）

### 2.1 帧结构

```
 Byte:     [0]   [1]   [2]    [3]   [4..N]      [N+1]    [N+2]
           HEAD  CMD   LENGTH SEQ   PAYLOAD    CRC16_L  CRC16_H
 Size:     1B    1B    1B     1B    0~32B       1B       1B

 总长度 = 6 + LENGTH（最小 6 字节，无 payload 时）
```

| 字段 | 大小 | 说明 |
|------|------|------|
| HEAD | 1B | 固定 `0xA5` |
| CMD | 1B | 命令码（见下表） |
| LENGTH | 1B | Payload 有效字节长度 |
| SEQ | 1B | 序列号（每次发送自增，回绕） |
| PAYLOAD | 0~32B | 可变长度数据 |
| CRC16_L | 1B | CRC16 低字节 |
| CRC16_H | 1B | CRC16 高字节 |

### 2.2 CRC16 参数（已验证双方一致）

| 参数 | 值 |
|------|-----|
| 算法 | CRC16-MODBUS |
| 多项式 | `0xA001`（反射形式） |
| 初始值 | `0xFFFF` |
| 输出异或 | `0x0000` |
| 字节序 | **低字节先发（Little-Endian）** |
| 校验范围 | HEAD(0xA5) ~ PAYLOAD 末字节（不含 CRC 本身） |

### 2.3 已验证的命令码定义

#### STM32(C板) → NUC（视觉端）

| CMD | 名称 | Hex | Payload 长度 | Payload 内容 | 当前状态 |
|-----|------|-----|-------------|--------------|---------|
| 0x10 | ARRIVE_BEAN | `ARRIVE_BEAN` | 0 | 无 | ✅ 正常发送 |
| 0x11 | ARRIVE_DIGIT | `ARRIVE_DIGIT` | 0 | 无 | 待实现 |
| 0x12 | RESET | `RESET` | 0 | 无 | 待实现 |
| 0x13 | PING | `PING` | 0 | 无 | 待实现 |
| 0x14 | ACK | `ACK` | 2 | `[acked_cmd, acked_seq]` | ✅ 正常回复 |

#### NUC（视觉端）→ STM32(C板)

| CMD | 名称 | Hex | Payload 长度 | Payload 内容 | 当前状态 |
|-----|------|-----|-------------|--------------|---------|
| 0x02 | FINAL_TASK | `FINAL_TASK` | 11 | 任务参数（待扩展） | 待联调 |
| 0x04 | BEAN_BIND | `BEAN_BIND` | 10 | 豆子坐标/ID | ✅ 收发正常 |
| 0x05 | PONG | `PONG` | 0 | 无 | 待联调 |
| 0x14 | ACK | `ACK` | 2 | `[acked_cmd, acked_seq]` | 待接收 |

---

## 三、已验证通过的通信流程

### 3.1 当前测试流程（已跑通）

```
时间轴 →

C板(STM32)                          NUC(视觉端)
  │                                    │
  │  ←─ 上电初始化 ──→                 │
  │                                    │
  │── A5 10 00 [seq] [CRC] ──────────→│  ARRIVE_BEAN (每2秒)
  │                              ✅ 解析成功
  │                              cmd=0x10, length=0
  │                                    │
  │←── A5 04 0A [seq] [payload][CRC] ─│  BEAN_BIND (手动触发)
  │  ✅ CDC_Receive_FS 触发            │  cmd=0x04, length=10
  │  ✅ proto_poll 解析成功             │
  │  ✅ CMD 匹配                        │
  │                                    │
  │── A5 14 02 [seq] 04 [ack_seq][CRC]→│  ACK 回复
  │                              ✅ 解析成功
  │                              cmd=0x14(ACK)
  │                              payload=[0x04, 0x01]
  │                              → 确认收到 BEAN_BIND(seq=1)
  │                                    │
```

### 3.2 完整业务流程（未来实现）

```
① C板发 ARRIVE_BEAN  →  NUC确认到达目标区
② NUC回 ACK          →  C板知道对方在线
③ NUC发 BEAN_BIND    →  C板获取豆子坐标/ID
④ C板回 ACK          →  NUC知道C板收到了
⑤ C板发 ARRIVE_DIGIT →  NUC确认到达放置区
⑥ NUC回 ACK
⑦ NUC发 FINAL_TASK   →  C板执行最终任务
⑧ C板回 ACK
```

---

## 四、C 板硬件引脚分配

### 4.1 USB CDC 通信

| 功能 | MCU 引脚 | 备注 |
|------|---------|------|
| USB DM | PA11 | USB OTG FS D- |
| USB DP | PA12 | USB OTG FS D+ |
| 供电 | 5V XT30 | 外部电池 |

### 4.2 NRF24L01（遥控器通信）

| NRF24L01 引脚 | C 板座子针脚 | MCU 引脚 | 信号名 |
|:---:|:---:|:---:|:---|
| VCC | 针脚 4 | 3.3V | 电源 |
| GND | 针脚 2 | GND | 地 |
| CE | **针脚 6** | **PF1** | 复用 I2C2_SCL 作为 GPIO |
| CSN | **针脚 1** | **PB12** | SPI2_CS（手动控制） |
| SCK | 针脚 3 | PB13 | SPI2_CLK |
| MISO | 针脚 7 | PB14 | SPI2_MISO |
| MOSI | 针脚 5 | PB15 | SPI2_MOSI |
| IRQ | 不接 | — | 轮询模式 |

> ⚠️ **重要**：PA4 和 PB0 被 BMI088 IMU 占用，不能用于 NRF24L01。

### 4.3 模式切换与 LED

| 功能 | MCU 引脚 | 说明 |
|------|---------|------|
| 用户按键 KEY | PA0 | 低电平有效，30ms 消抖，切换模式 |
| 红灯 PH12 | GPIO_OUTPUT | 🔴 遥控模式指示 / BEAN_BIND 收到诊断 |
| 绿灯 PH11 | GPIO_OUTPUT | 🟢 视觉模式指示 / proto_poll 诊断 |
| 蓝灯 PH10 | GPIO_OUTPUT | 🔵 NRF24L01 收包指示 / USB CDC 接收诊断 |

### 4.4 CAN 电机控制（待接）

| 功能 | MCU 引脚 | 用途 |
|------|---------|------|
| CAN1_TX | PB8 | DJI 3508 控制 |
| CAN1_RX | PB9 | DJI 3508 反馈 |
| CAN2_TX | PD0 | 预留 |
| CAN2_RX | PD1 | 预留 |

---

## 五、软件架构

### 5.1 FreeRTOS 任务列表

| 任务名 | 优先级 | 栈大小 | 功能 |
|--------|--------|--------|------|
| **defaultTask** | Normal | 512B | USB CDC 协议收发（主通信任务） |
| **motorTask** | High | 1024B | 电机控制（根据模式选择数据源） |
| **nrfTask** | Low | 1024B | NRF24L01 接收遥控器数据 |

### 5.2 文件结构

```
D:/CUBE MX project/receiver/
├── Core/Src/
│   ├── main.c                  # 主函数（CubeMX生成）
│   └── freertos.c              # ★ FreeRTOS任务定义（重点修改）
│
├── USB_DEVICE/App/
│   └── usbd_cdc_if.c           # ★ USB CDC 收发回调（关键修复）
│
├── protocol/                   # ★ 视觉串口协议模块
│   ├── protocol.h              #   帧结构/CMD定义/函数声明
│   └── protocol.c              #   CRC16查表 + 组帧proto_send + 解帧proto_poll
│
├── 2.4G/                       # NRF24L01 驱动模块
│   ├── 2.h                     #   引脚宏定义/寄存器地址/函数声明
│   └── 2.c                     #   SPI读写/Init/RX_Mode/RxPacket/Check
│
├── mode/                       # ★ 模式切换模块
│   ├── mode.h                  #   Mode_e枚举/接口声明
│   └── mode.c                  #   KEY消抖+模式切换+LED控制
│
├── module/
│   ├── dji_motor.h/c           # DJI 3508 电机驱动（CAN）
│   └── bsp_can.h/c             # CAN 板级支持包
│
├── bsp_can.c                   # CAN 过滤器配置
├── spi.c                       # SPI2 初始化（CubeMX生成，需确认引脚为PB13/14/15）
├── gpio.c                      # GPIO 初始化（CubeMX生成）
└── can.c                       # CAN 外设初始化
```

---

## 六、联调过程与问题记录

### 6.1 问题清单（已解决）

| # | 问题 | 严重程度 | 根因 | 解决方案 | 状态 |
|---|------|---------|------|---------|:---:|
| 1 | **CDC_Receive_FS 不存数据** | 🔴 致命 | 回调函数只做了 SetRxBuffer+ReceivePacket 准备下次接收，但从未将 `Buf` 数据复制到 `usb_rx_buf`，也未设置 `usb_rx_len` | 在回调中添加 `memcpy(usb_rx_buf, Buf, *Len)` 和 `usb_rx_len = *Len` | ✅ |
| 2 | ARMCC V5 C89 兼容性 | 🔴 编译错误 | Keil V5 编译器默认 C89 模式，不支持 `for(uint8_t i=...)` 和 `(uint8_t[]){...}` 复合字面量 | 变量提到块开头；复合字面量改为局部数组 | ✅ |
| 3 | NRF24L01 Init 缺上电延时 | 🟡 运行时风险 | RX_Mode 直接写 CONFIG 上电，未等 1.5ms 晶振稳定 | 加 `HAL_Delay(2)` 在 PWR_UP 后 | ✅ |
| 4 | PF1/PB12 GPIO 时钟未使能 | 🔴 运行时错误 | led_init() 只开了 GPIOH 时钟，没开 GPIOF/GPIOB | 补全 `__HAL_RCC_GPIOF_CLK_ENABLE()` 和 `__HAL_RCC_GPIOB_CLK_ENABLE()` | ✅ |
| 5 | CubeMX SPI2 引脚配错 | 🟡 配置错误 | 项目中 SPI2 配在 PI1/PI2/PI3，实际 C 板 8 针座映射到 PB13/14/15 | CubeMX 中修改 SPI2 引脚为 PB13(SCK)/PB14(MISO)/PB15(MOSI) | ✅ 需手动操作 |
| 6 | 遥控器摇杆 ADC 中点偏差 | 🟡 精度问题 | 硬编码 2048，实际中点 ~1850 | joystick.c 改为开机 64 次采样自校准 | ✅ |
| 7 | 遥控器 API 混用 F1/F4 | 🔴 编译错误 | main.c 中用了 F4 的 RCC 枚举名和 PeriphCLKSelection | 统一改为 F1 的 API | ✅ |
| 8 | 协议宏名不一致 | 🔴 编译错误 | 2.c 中用了 `CMD_FLUSH_RX` 但 2.h 定义的是 `NRF_CMD_FLUSH_RX` | 统一加 `NRF_` 前缀 | ✅ |

### 6.2 联调诊断方法（LED 三级指示）

| 诊断级别 | LED 颜色 | 位置 | 含义 | 对应代码位置 |
|---------|---------|------|------|------------|
| L1 物理层 | 🔵 蓝灯 | PH10 | USB CDC 收到 NUC 数据 | `usbd_cdc_if.c` → `CDC_Receive_FS()` |
| L2 解析层 | 🟢 绿灯 | PH11 | 进入 proto_poll 且有数据 | `protocol.c` → `proto_poll()` 入口 |
| L3 业务层 | 🔴 红灯 | PH12 | 匹配到 CMD_BEAN_BIND | `freertos.c` → switch case |

**诊断决策树：**

```
蓝灯?
 ├─ 否 → USB物理层没收到：检查USB线/驱动/tty设备/NUC串口号
 └─ 是 → 绿灯?
          ├─ 否 → usb_rx_len=0 或 proto_poll提前返回：
          │       检查CDC_Receive_FS是否正确存入了数据
          └─ 是 → 红灯?
                   ├─ 否 → 帧头/CRC/CMD不匹配：
                   │       检查双方协议参数是否一致
                   └─ 是 → 全链路通！检查ACK发送和NUC接收端
```

---

## 七、当前代码状态说明

### 7.1 已启用功能

| 功能 | 状态 | 说明 |
|------|:---:|------|
| USB CDC 接收 NUC 数据 | ✅ | CDC_Receive_FS 已修复数据存储 |
| USB CDC 发送到 NUC | ✅ | proto_send + CDC_Transmit_FS |
| proto_poll 解帧 | ✅ | 找帧头→校验长度→校验CRC→填充frame |
| proto_send 组帧 | ✅ | 填充HEAD/CMD/LEN/SEQ/Payload→计算CRC→发送 |
| ACK 回复 | ✅ | 收到 BEAN_BIND/FINAL_TASK 自动回 ACK |
| BEAN_BIND 接收 | ✅ | CMD=0x04 分支匹配正常 |
| 模式切换 | ✅ | KEY按键切换 遥控/视觉 模式 |
| NRF24L01 接收 | ✅ | nrfTask 循环收包存入 remote_data |
| LED 诊断 | ✅ | 蓝/绿/红三灯分别指示 L1/L2/L3 |

### 7.2 临时禁用的功能（调试完成后恢复）

| 功能 | 状态 | 原因 |
|------|:---:|------|
| ARRIVE_BEAN 周期发送 | ⏸️ 注释中 | 联调阶段排除 USB TX/RX 冲突 |

恢复方法：取消 `freertos.c` 中 `proto_send(CMD_ARRIVE_BEAN, NULL, 0)` 的注释。

### 7.3 待实现功能

| 功能 | 优先级 | 说明 |
|------|:---:|------|
| ARRIVE_DIGIT 发送 | 高 | 到达数字区后发送 |
| FINAL_TASK 处理 | 高 | 收到后解析 11 字节 payload |
| motorTask 电机控制 | 高 | 根据模式选择数据源控制 CAN 电机 |
| PING-PONG 心跳 | 中 | 链路保活检测 |
| 夹爪微调对接 | 中 | remote_data 摇杆数据 → 步进电机 |

---

## 八、给视觉端的参考信息

### 8.1 NUC 端测试命令

```bash
# 监听（持续接收并解析）
./serial_protocol_demo --port /dev/ttyACM0 --baudrate 115200 --listen

# 发送 BEAN_BIND 示例包
./serial_protocol_demo --port /dev/ttyACM0 --baudrate 115200 --send-bean-bind
```

### 8.2 C 板端编译注意事项

- **编译器**：ARMCC V5.06 update 7 (build 960)
- **C 语言标准**：C89（不支持 C99 特性）
- **关键限制**：
  - 变量声明必须在块 `{` 开头
  - 不能用 `for(int i=...)` 必须先 `int i; for(i=...)`
  - 不能用复合字面量 `(type[]){...}`
  - 注释不能用 `//` 嵌套（其实可以用，但建议统一风格）

### 8.3 C 板修改文件清单（最终版）

以下文件是本次联调中修改/新建的，按重要性排序：

| 序号 | 文件路径 | 修改内容 | 关键性 |
|:---:|----------|---------|:---:|
| 1 | `USB_DEVICE/App/usbd_cdc_if.c` | **修复 CDC_Receive_FS 数据存储缺失** + 蓝灯诊断 | 🔴 核心 |
| 2 | `Core/Src/freertos.c` | defaultTask 协议收发 + motorTask 模式分支 + nrfTask 收包 + 红灯诊断 | 🔴 核心 |
| 3 | `protocol/protocol.c` | CRC16 查表 + proto_send 组帧 + proto_poll 解帧 + 绿灯诊断 | 🔴 核心 |
| 4 | `protocol/protocol.h` | ProtoFrame_t 结构体 + CMD 定义 + 函数声明 | 🔴 核心 |
| 5 | `2.4G/2.c` | NRF24L01 驱动重写（完整Init/RX_Mode/RxPacket） | 🟡 重要 |
| 6 | `2.4G/2.h` | NRF24L01 寄存器定义 + C板引脚宏(PB12/PF1) | 🟡 重要 |
| 7 | `mode/mode.c` | 模式切换逻辑（KEY按键+LED控制） | 🟡 重要 |
| 8 | `mode/mode.h` | Mode_e 枚举 + 接口声明 | 🟡 重要 |
| 9 | `Remote_Controller/joystick/joystick.c` | 摇杆自校准（64次平均取中点） | 🟢 遥控器端 |
| 10 | `Remote_Controller/Core/Src/main.c` | 遥控器正式版（全输入+NRF发送+OLED显示） | 🟢 遥控器端 |
| 11 | `Remote_Controller/2.4G/2.c` | 修正 EN_AA 注释错误 | 🟢 遥控器端 |

---

## 九、Payload 扩展指南（面向未来的复杂化）

当前版本使用简化 payload，后续扩展时需注意：

### 9.1 已定义的 Payload 格式

| CMD | 当前 Length | 未来扩展方向 |
|-----|-----------|-------------|
| BEAN_BIND (0x04) | **10 字节** | 豆子 ID(2) + 坐标 X(2) + Y(2) + Z(2) + 类型(1) + 状态(1) = 10B |
| FINAL_TASK (0x02) | **11 字节** | 任务类型(1) + 目标 X(2) + Y(2) + Z(2) + 参数(4) = 11B |
| ACK (0x14) | **2 字节** | `[acked_cmd, acked_seq]` — 固定不变 |
| ARRIVE_BEAN (0x10) | **0 字节** | 可能加状态标志（如电池电量/信号强度） |

### 9.2 扩展时的兼容性要求

1. **CMD 码不能改**——已经对联确认过
2. **LENGTH 字段必须准确**——proto_poll 依赖它计算总帧长
3. **CRC 覆盖范围不变**——始终覆盖 HEAD ~ PAYLOAD 末尾
4. **如果 payload 结构变化**只需修改：
   - C 板 `freertos.c` 的 `switch(frame.cmd)` 里各 case 的解析逻辑
   - NUC 端对应的 payload 构造/解析
5. **proto_send/proto_send/proto_poll 不需要改**——它们是通用的组帧/解帧框架

### 9.3 建议的 Payload 定义方式

```c
/* ===== C 板侧建议 ===== */

/* BEAN_BIND Payload: 10 字节 */
typedef struct __attribute__((packed)) {
    uint16_t bean_id;       /* 豆子唯一 ID */
    int16_t  x;             /* 世界坐标 X (mm) */
    int16_t  y;             /* 世界坐标 Y (mm) */
    int16_t  z;             /* 世界坐标 Z (mm) / 高度 */
    uint8_t  type;          /* 豆子类型/颜色 */
    uint8_t  status;        /* 状态标志 */
} BeanBindPayload_t;        /* sizeof = 10 */

/* FINAL_TASK Payload: 11 字节 */
typedef struct __attribute__((packed)) {
    uint8_t  task_type;     /* 任务类型码 */
    int16_t  target_x;      /* 目标 X */
    int16_t  target_y;      /* 目标 Y */
    int16_t  target_z;      /* 目标 Z */
    uint32_t param;         /* 扩展参数 */
} FinalTaskPayload_t;       /* sizeof = 11 */
```

> ⚠️ 以上结构体定义仅供参考。**具体字段语义需与视觉端逐字节对齐确认**后才能正式使用。

---

## 十、常见故障排查速查

| 现象 | 可能原因 | 排查方法 |
|------|---------|---------|
| NUC 端 `/dev/ttyACM*` 不存在 | USB线只供电无数据 / 驱动问题 | 换 USB 线；`dmesg \| grep tty` 查看 |
| NUC 收不到 ARRIVE_BEAN | C板不在视觉模式 | 按 C板 KEY 切换到🟢绿灯 |
| NUC 发了但 C板 不回 ACK | 见六级 LED 诊断 | 观察蓝/绿/红三灯状态 |
| 蓝灯不闪 | USB 物理层没收到 | 检查 NUC 串口号是否正确 |
| 蓝灯闪绿灯不闪 | usb_rx_len=0 | 检查 CDC_Receive_FS 是否存了数据 |
| 蓝绿灯都闪红灯不闪 | CRC校验失败或CMD不匹配 | 双方 CRC 参数是否一致 |
| 三灯都闪但 NUC 没收到 ACK | CDC_Transmit_FS 返回 BUSY | 检查是否有并发发送冲突 |
| NRF24L01 红灯闪烁 | 模块离线/接线错误 | 检查 8 针座接线；CE/CSN 引脚 |
| 遥控器摇杆数值偏 | ADC中点不准 | 已用自校准解决；重启遥控器重校准 |

---

## 附录 A：C 板 Keil 工程配置备忘

### Include Paths 需要添加

```
../protocol
../mode
../2.4G
../module
```

### Source Files 需要添加

```
protocol/protocol.c
mode/mode.c
2.4G/2.c
module/dji_motor.c
module/bsp_can.c
```

### CubeMX 需确认的引脚配置

| 外设 | 引脚 | 模式 |
|------|------|------|
| SPI2_SCK | PB13 | AF5 |
| SPI2_MISO | PB14 | AF5 |
| SPI2_MOSI | PB15 | AF5 |
| PB12 | GPIO_Output (CSN) |
| PF1 | GPIO_Output (CE) |
| PA0 | GPIO_Input (KEY) |
| PH10/PH11/PH12 | GPIO_Output (LED) |

---

*文档结束*
