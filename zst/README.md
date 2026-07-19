# 起重机物流视觉工程

该文档按程序真实执行顺序讲解相机、OpenVINO YOLO、多帧投票、A/B组状态机、
CMD/DATA封装、CRC和Linux串口，并给出推荐源码阅读路线。

本工程参考桌面 `26new飞镖` 的代码组织方式：

- `CameraDriver`：工业相机封装。
- `ImgProcessing`：OpenVINO YOLO、多帧收集和任务规划。
- `Communication`：串口通信框架。
- `Tool`：配置和工具函数。
- `config`：运行参数。

核心流程：

```text
MindVision工业相机
-> OpenVINO直接加载best5.onnx并在Intel设备上推理YOLO
-> 根据规则生成视觉决策
-> 串口框架发送给电控
```

## YOLOv8 推理数据流

本工程不在 C++ 中直接加载 `.pt`，而是加载由 Ultralytics YOLOv8 导出的
`best5.onnx`。不需要先转换成OpenVINO的`.xml/.bin`，OpenVINO可以直接读取ONNX。
一帧图像的处理顺序如下：

```text
相机 BGR 原图
-> 等比例缩放并补 114 灰边（letterbox）
-> BGR 转 RGB、像素归一化到 0~1、HWC 转 CHW
-> OpenVINO输入 [1, 3, 640, 640]
-> 解析 [1, 12, 8400] / [1, 8400, 12]
-> 置信度过滤
-> 坐标从 letterbox 画布还原到相机原图
-> 按类别执行 NMS
-> 生成 Detection 交给多帧投票器和任务规划器
```

本项目的 12 个输出通道为 `4 个框参数 + 8 个类别分数`。8 个类别的顺序必须
与训练数据集 YAML 中的 `names` 完全一致：黄豆、绿豆、白芸豆、数字 1~5。
`model.device: AUTO`会让OpenVINO选择当前NUC上可用的Intel CPU/GPU/NPU；如果只想
使用CPU，可改成`CPU`。首次运行会把设备编译结果写入`runtime/openvino_cache`，
后续运行在模型、设备和OpenVINO版本不变时直接复用缓存，因此模型载入会更快。

程序启动时会打印`best5.onnx`的真实输入/输出形状，并严格检查单输入、单输出、
float32和`[1,3,640,640]`。如果换错模型，会在初始化阶段明确报错，不会继续越界解析。

### debug模式识别与发送日志

把`runtime.mode`改为`debug`后，相机会直接打开，并增加以下调试信息：

```text
[数字顺序核对] 通过，15/20帧从右到左一致=2 -> 4 -> 5 -> 1
[数字推断] place5不可见，15-前四个数字之和=3，已写入place5
[Debug最终推断] 数字布局=[place1=2, place2=4, place3=5, place4=1, place5=3]
[Debug待发送] CMD=0x21，DATA(十进制)=[2 4 5 1 3]
[Serial] TX A6 21 02 04 05 01 03 CRC_L CRC_H
```

单帧YOLO结果不再写终端，只在OpenCV窗口中通过检测框和`YOLO R-L`实时显示；
终端只保留多帧稳定核对、排序、正式缓存、阶段切换和串口发送。连续多轮出现相同的
“候选为空/未凑齐/重复豆子”状态时只提示第一次，候选组成变化后才重新提示。

## 两队双向通信与识别结果协议

两个队伍使用不同 C 板但不会同时上场，因此保留一个串口驱动，通过
`config/vision.yaml` 的 `workflow.team_mode` 选择流程。调试窗口中也可以按
字母键 `A`/`B` 切换；切换会清空旧识别缓存。
`team_mode` 只在视觉电脑内部选择状态机，不会作为字节发送给电控。

视觉识别结果使用固定命令长度的无ACK简化格式：

```text
A6 CMD DATA... CRC_L CRC_H
```

串口参数仍为 `115200-8-N-1`。CRC与电控 `referee.c`、桌面“26new飞镖”一致：
初值 `0xFFFF`、反向多项式 `0x8408`、低字节先发送。`simulated: true` 时只打印帧，
不访问设备。

- `A6`：固定帧头。
- `CMD`：命令码，同时决定后面DATA的固定长度。
- `DATA`：全部为 `uint8_t`，直接按协议顺序写入字节数组。
- `CRC_L/CRC_H`：覆盖A6、CMD和全部DATA，低字节先发。
- `VERSION、TEAM、SESSION、SEQUENCE、LENGTH` 均不发送；两个队伍由不同C板和
  `workflow.team_mode` 区分，每个CMD的数据长度固定。

固定命令表：

| CMD | 用途 | DATA长度 | 完整帧长度 |
|---|---|---:|---:|
| `0x10` | A组豆子位置：黄/绿/白 | 3 | 7 |
| `0x11` | A组数字位置：数字1/2/3 | 3 | 7 |
| `0x20` | B组当前中心豆子码 | 1 | 5 |
| `0x21` | B组五个位置的数字 | 5 | 9 |

### 电控发给视觉：相机开关命令

电控到视觉使用另一个帧头 `0x5A`，当前只有一个 `uint8_t camera_state`，因此不再
增加CMD和长度字段：

```text
5A CAMERA_STATE CRC_L CRC_H
```

| 字节下标 | 类型 | 含义 |
|---:|---|---|
| 0 | `uint8_t` | 固定帧头 `0x5A` |
| 1 | `uint8_t` | `camera_state`：0关闭相机，1打开相机 |
| 2 | `uint8_t` | CRC16低字节 |
| 3 | `uint8_t` | CRC16高字节 |

CRC算法与视觉发送方向完全相同，也是电控裁判系统CRC16：初值 `0xFFFF`、反向
多项式 `0x8408`。CRC覆盖前两个字节 `[0x5A][camera_state]`。固定测试帧为：

```text
关闭相机：5A 00 3F DE
打开相机：5A 01 B6 CF
```

电控STM32发送参考代码如下。若使用普通UART，调用 `HAL_UART_Transmit()`；若使用
USB CDC，只需把最后一行替换成项目自己的CDC发送函数，线路4字节不变。

```c
#include <stdint.h>
#include "usart.h"

/*
 * 计算与视觉电脑、referee.c完全一致的裁判系统CRC16。
 * data：需要参与CRC计算的字节数组。
 * length：参与计算的字节数；相机命令固定传入2，即帧头和camera_state。
 */
static uint16_t Vision_CRC16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;                    /* 协议规定的CRC初值 */

    for (uint16_t i = 0U; i < length; ++i) {
        crc ^= data[i];                        /* 当前字节异或到CRC低8位 */
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            if ((crc & 0x0001U) != 0U) {
                crc = (uint16_t)((crc >> 1U) ^ 0x8408U);
            } else {
                crc >>= 1U;
            }
        }
    }
    return crc;
}

/*
 * 给视觉电脑发送相机状态。
 * camera_state=0：关闭工业相机，但视觉程序和串口继续运行。
 * camera_state=1：重新打开工业相机，并从之前保存的A/B识别阶段继续。
 * 其他数值没有定义，函数直接拒绝发送，避免电控变量异常时误操作相机。
 */
void Vision_SendCameraState(uint8_t camera_state)
{
    uint8_t frame[4] = {0x5AU, camera_state, 0x00U, 0x00U};
    uint16_t crc;

    if (camera_state > 1U) {
        return;                                /* 只允许0或1 */
    }

    crc = Vision_CRC16(frame, 2U);             /* CRC只覆盖5A和camera_state */
    frame[2] = (uint8_t)(crc & 0x00FFU);       /* 先发送CRC低字节 */
    frame[3] = (uint8_t)((crc >> 8U) & 0x00FFU); /* 后发送CRC高字节 */

    /* huart1需要替换为电控工程实际连接NUC的UART句柄。 */
    (void)HAL_UART_Transmit(&huart1, frame, sizeof(frame), 20U);
}
```

### 当前无ACK模式的注意事项

算法端由 `VirtualSerial::receiveCameraState()` 非阻塞解析相机控制帧。串口一次只
收到半帧时会缓存到下一轮；一次收到多帧时会逐帧处理；帧头或CRC错误时不会
开关相机。关闭相机只停止取图和推理，不会关闭串口、退出程序或清空已经识别的
数字/豆子状态。配置必须使用 `serial.simulated: false` 才会真正接收电控数据；
`rx_log: true` 会打印收到的相机控制帧。

视觉结果发送后立即推进流程并保存断点，不等待电控确认。因此“Linux串口写入成功”
不等于“电控业务代码已经解析成功”。联调时必须同时观察视觉TX十六进制日志和电控
接收变量；如果以后需要自动确认与丢包重发，再单独恢复SEQ+ACK协议。

### 调试模式与比赛模式

相机启动方式由 `config/vision.yaml` 的 `runtime.mode` 切换：

```yaml
runtime:
  mode: "competition"   # competition=比赛；debug=日常调试
```

比赛模式 `competition`：程序先打开串口，工业相机保持关闭，然后等待电控命令。
只有收到CRC正确的 `5A 01 B6 CF` 后才调用MindVision SDK打开相机；收到
`5A 00 3F DE` 后关闭相机，但串口和程序主循环继续运行。再次收到开启命令
时，可以继续原来的A/B识别阶段。实机比赛还必须设置：

```yaml
serial:
  simulated: false
runtime:
  mode: "competition"
```

调试模式 `debug`：程序启动后直接打开相机，不等待电控，并忽略收到的
`camera_state=0/1` 开关命令，避免日常调试时相机被意外关闭。OpenCV窗口获得焦点后，
按 `0` 关闭相机、按 `1` 打开相机；按 `A`/`B` 切换队伍，按 `R` 清空本场结果。
没有连接电控时推荐：

```yaml
serial:
  simulated: true       # 结果只打印，不访问串口硬件
runtime:
  mode: "debug"         # 启动后直接打开相机
```

调试模式并没有删除串口代码：如果使用 `simulated: false`，视觉仍会发送识别结果，
只是不再让电控控制相机开关。若填写了 `debug/competition` 之外的字符串，
程序会报告配置错误并退出，防止比赛时因为拼写错误意外打开相机。

TeamA 识别与发送顺序：

```text
1. 识别3个豆子，并按画面从右到左保存物理位置1~3；立即发送CMD=0x10和3字节DATA。
2. 识别5个数字箱，并按画面从右到左保存place1~place5上的数字。
3. 数字完成后发送CMD=0x11和第二组3字节DATA。
```

A组两条消息的DATA固定定义如下，顺序绝对不能交换：

```text
CMD=0x10: [黄豆位置, 绿豆位置, 白芸豆位置]
CMD=0x11: [数字1位置, 数字2位置, 数字3位置]
```

例如豆子从右到左为“绿、黄、白”，保存的原始豆子数组是
`[绿豆, 黄豆, 白芸豆]`，反向查询后前三字节为 `[2,1,3]`。数字从右到左为
`[5,2,3,1,4]`，数字1、2、3分别位于place4、place2、place3，后三字节为
`[4,2,3]`。两次发送分别为：

```text
豆子帧 = A6 10 02 01 03 8E BC
数字帧 = A6 11 04 02 03 84 5C
```

上例豆子帧CRC16为`0xBC8E`，数字帧CRC16为`0x5C84`；线路低字节在前。

数字4和5不进入A组最终DATA，但仍要完成识别，用于确认五个箱位布局完整。

电控分别校验两条7字节帧后，可以把两个3字节DATA保存到下面的结构体：

```c
/* A组CMD=0x10：豆子识别完成后接收。 */
typedef struct
{
    uint8_t soybean_place;       /* DATA[0]：黄豆位于三个豆子位置中的第几个，范围1~3 */
    uint8_t mung_bean_place;     /* DATA[1]：绿豆位于三个豆子位置中的第几个，范围1~3 */
    uint8_t white_bean_place;    /* DATA[2]：白芸豆位于三个豆子位置中的第几个，范围1~3 */
} TeamABeanPositionsData;

/* A组CMD=0x11：数字识别完成后接收。 */
typedef struct
{
    uint8_t soybean_box_place;   /* DATA[0]：数字1所在箱位，即黄豆应放到place几，范围1~5 */
    uint8_t mung_bean_box_place; /* DATA[1]：数字2所在箱位，即绿豆应放到place几，范围1~5 */
    uint8_t white_bean_box_place;/* DATA[2]：数字3所在箱位，即白芸豆应放到place几，范围1~5 */
} TeamADigitPositionsData;
```

不要把前3字节理解成豆子类型编码；它们现在是“位置”。例如`02 01 03`表示黄豆在
第2位、绿豆在第1位、白芸豆在第3位。

TeamB 消息顺序：

```text
1. 识别画面中心的第一个豆子，稳定后发送它的BeanCode（1/2/3）并记忆类型
2. 识别全部5个数字，发送 DigitLayout，DATA = place1~place5上的数字
3. 继续识别画面中心尚未记忆的豆子，每识别一种就发送它的BeanCode
4. 三种豆子均已记忆并发送后，流程结束
```

豆子出现顺序不固定。中心是黄豆就发送 `01`，中心是绿豆就发送 `02`，中心是
白芸豆就发送 `03`。已经记忆过的类型再次来到中心，只参与重复判断，不会再次
写入状态或发送；程序也不会因为中心豆子重复而改选旁边的其他豆子。

### A/B组豆子排序规则

- A组采用完整排列投票：首次同帧识别出黄豆、绿豆、白芸豆后才启动20帧窗口；完整帧
  按检测框中心X从右到左形成一张顺序票。同一种完整顺序至少出现15帧，才一次性写入
  `beanPlaces[0..2]`；缺少任何豆子的帧不计票，顺序票不足时本轮一个也不保存。
- B组不使用左右排序：从画面中的所有豆子里选择距离画面中心最近的一个，
  只要它位于配置的中心区域内，就允许参加多帧投票，类型顺序不固定。
- B组其余豆子完全忽略，不写入 `beanPlaces`，也不标记为已经识别；以后它们移动到
  画面中心时仍然可以在对应阶段被识别。
- B组会记忆已经稳定识别的豆子类型。重复类型再次出现在中心时不重复发送，也不会
  越过它选择旁边未识别的豆子；需要让新的豆子真正移动到中心后再识别。
- 若20帧投票期间中心类别有抖动，只取出现次数最多的类别；第一名并列时本轮不发送。
  若第一名已经记忆，也不发送，不会退而选择票数第二的类别。

两组每轮稳定投票都会输出候选和正式保存日志，识别候选不等于已经保存：

```text
[A组豆子顺序核对] 完整3目标帧=17/20，最高一致顺序票=15/15
[A组豆子顺序核对] 通过，15/20帧从右到左一致=绿豆 -> 黄豆 -> 白芸豆

[B组中心豆子核对] 稳定候选=黄豆(...)
[B组中心豆子识别] 最终确认=黄豆，命中=...，平均X=...
[B组豆子保存] 新增=1，正式缓存=[位置1=黄豆(编码1)]，完成=否
```

中心区域由 `config/vision.yaml` 配置：

```yaml
workflow:
  team_b_center_width_ratio: 0.40
```

`0.40` 表示只接受画面横向中间40%的范围，即大约 `30%～70%`。该参数只影响
B组豆子阶段，不影响B组数字识别，也不影响A组。允许范围为 `0.05～1.00`，程序
会自动限制越界配置。

### A/B共用的数字推理规则

数字缓存不能按照“哪个数字先被YOLO识别到”决定顺序。A、B两组共用下面的算法：

1. 首次同帧恰好识别出4个不同数字后，才启动 `vote_frames_per_stage` 帧观察窗口。
2. 同一个数字在同一帧即使出现多个重复框，也只取置信度最高的一个。
3. 只有同一帧恰好出现4个不同数字时，才按检测框中心X从右到左形成一张顺序票；
   少于4个或误检出5个的帧均不计票。
4. 同一种完整顺序至少获得 `min_consistent_order_frames` 票后，才一次性写入place1~place4。
5. 利用数字1~5总和为15，以 `15-前四位数字之和` 推断不可见的place5。

配置如下：

```yaml
workflow:
  vote_frames_per_stage: 20
  min_hits_per_stage: 6             # 仅B组中心单豆使用
  min_consistent_order_frames: 15   # A组3豆和数字4目标的完整顺序票阈值
```

例如实际可见画面从右到左是 `2 4 5 1`。20帧中有15帧完整出现并保持这个顺序，
另外5帧缺目标或顺序不同，则多数顺序达到阈值并整批保存：

```text
[数字顺序核对] 通过，15/20帧从右到左一致=2 -> 4 -> 5 -> 1
[数字推断] place5不可见，15-前四个数字之和=3，已写入place5
[数字缓存] 已保存=5/5，当前数组=[2 4 5 1 3]
```

该推理流程不需要切换第二角度；A组数字位置通过CMD=0x11发送，B组仍发送5字节数字数组。

以下“位置 -> 数字”的5字节数组只用于B组。A组会将其反向查询为数字1、2、3
各自所在的位置。例如画面从右到左
五个位置识别为 `4 1 2 5 3`，发送的 DATA 就是：

```text
04 01 02 05 03
```

实际顺序为“绿豆 -> 数字 -> 黄豆 -> 白芸豆”时，B组依次发送：

```text
绿豆： A6 20 02 CRC_L CRC_H
数字： A6 21 04 01 02 05 03 CRC_L CRC_H
黄豆： A6 20 01 CRC_L CRC_H
白芸豆：A6 20 03 CRC_L CRC_H
```

算法端使用 `VisionTxPacket` 分别保存`command`和`data`；
`VirtualSerial` 再按`[A6][CMD][DATA...][CRC]`封帧发送。

## 终端日志

日志只在稳定投票提交、状态切换和串口发送时输出，不会每帧打印 YOLO 框，
避免几十帧每秒刷屏。主要日志包括：

```text
[B组投票] 当前阶段是否新增稳定结果
[数字顺序核对] 完整4目标帧数、最高一致顺序票和最终从右到左顺序
[数字缓存] 顺序投票通过后整批提交的正式数字数组
[数字位置] place1~place5上分别识别到什么数字
[豆子结果] 豆子名称、发送码、对应数字、该数字位于第几个位置
[A组豆子位置] 按黄豆、绿豆、白芸豆固定顺序显示三个物理位置
[A组豆子位置] 显示CMD=0x10的3字节DATA
[A组数字位置] 显示CMD=0x11的3字节DATA
[发送准备] 当前电脑模式、CMD和十进制DATA
[Serial] TX ... 完整十六进制帧及真实/模拟发送结果
```

例如第一个中心豆子是绿豆，数字布局为 `4 1 2 5 3`，日志会显示：

```text
[数字位置] [place1=4, place2=1, place3=2, place4=5, place5=3]
[豆子结果] 第一个豆子位置匹配：豆子=绿豆，发送码=2，对应数字=2，数字位于第3个位置(place3)
[豆子结果] 新的中心豆子识别完成：豆子=黄豆，发送码=1，对应数字=1，数字位于第2个位置(place2)
[豆子结果] 新的中心豆子识别完成：豆子=白芸豆，发送码=3，对应数字=3，数字位于第5个位置(place5)
```

这里“对应数字和位置”只用于视觉终端日志，B 组发给电控的豆子消息仍然只有
单字节 `01`、`02`、`03`；数字数组单独发送一次。

当前使用无ACK双向通信：视觉接收电控的相机开关命令，视觉识别结果单向发送给电控。
结果生成后立即推进状态并保存断点，不等待电控回复，也不会自动重发业务结果。

比赛规则映射：

```text
黄豆 -> 数字 1 货箱
绿豆 -> 数字 2 货箱
白芸豆 -> 数字 3 货箱
数字 4、5 为空箱
```

## 识别结果日志与相机自动恢复

终端日志以比赛识别结果为主：箱位全部稳定后输出 `place1~place5` 上的数字；
豆子稳定后输出豆子名称、发送编码、对应数字以及该数字所在箱位。程序不再每秒
打印FPS、取帧耗时和推理延迟，调试画面也不再叠加这些性能数据。

相机恢复参数：

```yaml
camera:
  frame_timeout_ms: 200
  reconnect_after_failures: 5
```

连续取帧失败达到阈值后，程序会自动关闭并重新打开相机。失败期间仍处理
`cv::waitKey()`，因此窗口不会再因为取帧失败而完全无响应，仍可按 `q` 或 `ESC`退出。

### 启动自动清理旧断点

只关闭或断开相机、但不退出程序时，工作流状态仍保存在内存中：例如A组豆子已经
识别完成，收到`camera_state=0`关闭相机后串口和程序仍运行；再次收到
`camera_state=1`时会继续等待数字，不会清理当前比赛结果。

为了避免新比赛忘按`R`而恢复上场数据，默认每次完整启动程序都会自动删除正式断点
和可能残留的`.tmp`临时文件，然后从空状态开始：

```yaml
workflow:
  clear_progress_on_start: true
  resume_progress: true
  progress_file: "runtime/workflow_progress.txt"
```

正常启动日志如下：

```text
[断点管理] 启动策略=自动清理，不加载上场比赛进度
[断点管理] 已自动删除旧文件: .../runtime/workflow_progress.txt
[Workflow] 从头开始, mode=team_a
```

程序仍会保存本场断点，但下一次启动默认先删除，不会读取。如果比赛中途程序异常退出，
并且明确需要恢复当前比赛，可在重启前临时改成：

```yaml
workflow:
  clear_progress_on_start: false
  resume_progress: true
```

恢复完成后，下场比赛前必须重新改回`true`。键盘`R`仍然保留，可在程序运行期间
手动立即清空当前内存、正式断点和`.tmp`文件，但正常新比赛已经不需要依赖按`R`。

如果只想在程序停止时手工清理，也可以执行下面命令；默认自动策略下通常不需要：

```bash
rm -f ~/zst/runtime/workflow_progress.txt ~/zst/runtime/workflow_progress.txt.tmp
```

注意：`serial.simulated: true`时没有真实C板，程序只打印完整发送帧，不访问串口；
无ACK状态机会正常继续并保存断点。
正式比赛只需确认`clear_progress_on_start: true`和`simulated: false`。

为得到稳定帧率，比赛现场建议固定曝光，例如：

```yaml
camera:
  exposure_us: 8000
  gain: 8
  auto_exposure: false
```

自动曝光在暗场景下可能主动拉长曝光时间，从而降低相机帧率。实际数值需要根据现场
亮度调整，不能只为了帧率把画面调得过暗。虚拟机USB转发也可能造成卡帧，最终性能
应以真实NUC运行数据为准。

Ubuntu 编译依赖：

```bash
sudo apt update
sudo apt install -y build-essential cmake libopencv-dev libyaml-cpp-dev
```

还需要安装OpenVINO开发包。若使用Intel官方安装目录，每次打开新终端后先载入环境：

```bash
source /opt/intel/openvino/setupvars.sh
```

如果实际安装位置不同，请把上面的路径换成真实`setupvars.sh`路径。该脚本会设置
`OpenVINO_DIR`和运行库路径，使CMake能够找到`openvino/openvino.hpp`及
`openvino::runtime`。

NUC目录示例（用户名和安装位置可以任意）：

```text
/任意目录/
  zst/                              # 本项目
  linuxSDK_版本号/                    # MindVision Linux SDK根目录
    include/CameraApi.h
    lib/x64/libMVSDK.so
```

编译示例：

```bash
mkdir -p build
cd build
cmake ..
make -j
```

推荐在NUC首次配置时明确指定相机SDK根目录。路径只保存在build的CMake缓存中，
不会写死进源码：

```bash
cmake .. \
  -DMINDVISION_ROOT=/你的/linuxSDK_V2.1.0.49202602041120
```

如果没有执行`setupvars.sh`，也可以显式给出OpenVINO的CMake配置目录：

```bash
cmake .. \
  -DOpenVINO_DIR=/实际路径/openvino/runtime/cmake \
  -DMINDVISION_ROOT=/实际路径/linuxSDK_版本号
```

源码中的项目头文件全部使用相对包含，例如：

```cpp
#include "ImgProcessing/VisionSystem.h"
#include "Communication/VirtualSerial.h"
#include <openvino/openvino.hpp>
#include <CameraApi.h>
```

如果曾用旧的绝对路径生成过build目录，必须清理旧CMake缓存后重新配置：

```bash
cd /你的/zst
rm -rf build
mkdir build && cd build
cmake .. \
  -DOpenVINO_DIR=/实际路径/openvino/runtime/cmake \
  -DMINDVISION_ROOT=/实际路径/MindVision-SDK根目录
make -j"$(nproc)"
```

运行示例：

```bash
./logistics_vision ../config/vision.yaml
```
