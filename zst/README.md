# 起重机物流视觉工程

该文档按程序真实执行顺序讲解相机、YOLO、SVM、多帧投票、A/B组状态机、
CMD/DATA封装、CRC和Linux串口，并给出推荐源码阅读路线。

本工程参考桌面 `26new飞镖` 的代码组织方式：

- `CameraDriver`：工业相机封装。
- `ImgProcessing`：YOLO ONNX Runtime、SVM、任务规划。
- `Communication`：串口通信框架。
- `Tool`：配置和工具函数。
- `config`：运行参数。

核心流程：

```text
MindVision工业相机
-> ONNX Runtime CPU 推理 YOLO
-> SVM 复核豆子 ROI
-> 根据规则生成视觉决策
-> 串口框架发送给电控
```

## YOLOv8 推理数据流

本工程不在 C++ 中直接加载 `.pt`，而是加载由 Ultralytics YOLOv8 导出的
`best.onnx`。一帧图像的处理顺序如下：

```text
相机 BGR 原图
-> 等比例缩放并补 114 灰边（letterbox）
-> BGR 转 RGB、像素归一化到 0~1、HWC 转 CHW
-> ONNX Runtime 输入 [1, 3, 640, 640]
-> 解析 [1, 12, 8400] / [1, 8400, 12]
-> 置信度过滤
-> 坐标从 letterbox 画布还原到相机原图
-> 按类别执行 NMS
-> 生成 Detection 交给 SVM、投票器和任务规划器
```

本项目的 12 个输出通道为 `4 个框参数 + 8 个类别分数`。8 个类别的顺序必须
与训练数据集 YAML 中的 `names` 完全一致：黄豆、绿豆、白芸豆、数字 1~5。
当前 ONNX Runtime 会话使用 CPU；安装 CUDA 本身不会自动让这里切换到 GPU。

## 两队双向通信与识别结果协议

两个队伍使用不同 C 板但不会同时上场，因此保留一个串口驱动，通过
`config/vision.yaml` 的 `workflow.team_mode` 选择流程。调试窗口中也可以按
数字键 `1`/`2` 切换；切换会清空旧识别缓存。
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
- `DATA`：全部为 `uint8_t`，不使用ByteConverter。
- `CRC_L/CRC_H`：覆盖A6、CMD和全部DATA，低字节先发。
- `VERSION、TEAM、SESSION、SEQUENCE、LENGTH` 均不发送；两个队伍由不同C板和
  `workflow.team_mode` 区分，每个CMD的数据长度固定。

固定命令表：

| CMD | 用途 | DATA长度 | 完整帧长度 |
|---|---|---:|---:|
| `0x10` | A组最终位置结果 | 6 | 10 |
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
`camera_state=0/1` 开关命令，避免日常调试时相机被意外关闭。没有连接电控时推荐：

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
1. 识别3个豆子，并按画面从左到右保存物理位置1~3；本阶段不发送。
2. 识别5个数字箱，并按画面从左到右保存place1~place5上的数字。
3. 两部分均完成后，只发送一次CMD=0x10和6字节DATA。
```

A组DATA固定定义如下，顺序绝对不能交换：

```text
[黄豆位置, 绿豆位置, 白芸豆位置, 数字1位置, 数字2位置, 数字3位置]
```

例如豆子从左到右为“绿、黄、白”，保存的原始豆子数组是
`[绿豆, 黄豆, 白芸豆]`，反向查询后前三字节为 `[2,1,3]`。数字从左到右为
`[5,2,3,1,4]`，数字1、2、3分别位于place4、place2、place3，后三字节为
`[4,2,3]`。最终发送：

```text
DATA = [2,1,3,4,2,3]
完整帧 = A6 10 02 01 03 04 02 03 73 ED
```

上例CRC16为`0xED73`，线路仍按低字节在前发送，所以末尾是`73 ED`。

数字4和5不进入A组最终DATA，但仍要完成识别，用于确认五个箱位布局完整。

电控在校验完整10字节帧CRC通过后，可以把6字节DATA保存到下面的结构体。该结构体
只表示业务DATA，不包含帧头、CMD和CRC：

```c
/* A组CMD=0x10的固定6字节业务数据；每个位置值的有效范围都是1~5或1~3。 */
typedef struct
{
    uint8_t soybean_place;       /* DATA[0]：黄豆位于三个豆子位置中的第几个，范围1~3 */
    uint8_t mung_bean_place;     /* DATA[1]：绿豆位于三个豆子位置中的第几个，范围1~3 */
    uint8_t white_bean_place;    /* DATA[2]：白芸豆位于三个豆子位置中的第几个，范围1~3 */
    uint8_t soybean_box_place;   /* DATA[3]：数字1所在箱位，即黄豆应放到place几，范围1~5 */
    uint8_t mung_bean_box_place; /* DATA[4]：数字2所在箱位，即绿豆应放到place几，范围1~5 */
    uint8_t white_bean_box_place;/* DATA[5]：数字3所在箱位，即白芸豆应放到place几，范围1~5 */
} TeamAResultData;
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

- A组保持原逻辑：一个画面中有多个稳定豆子时，根据检测框中心X坐标从左到右
  排序并保存。
- B组不使用从左到右排序：从画面中的所有豆子里选择距离画面中心最近的一个，
  只要它位于配置的中心区域内，就允许参加多帧投票，类型顺序不固定。
- B组其余豆子完全忽略，不写入 `beanPlaces`，也不标记为已经识别；以后它们移动到
  画面中心时仍然可以在对应阶段被识别。
- B组会记忆已经稳定识别的豆子类型。重复类型再次出现在中心时不重复发送，也不会
  越过它选择旁边未识别的豆子；需要让新的豆子真正移动到中心后再识别。
- 若20帧投票期间中心类别有抖动，只取出现次数最多的类别；第一名并列时本轮不发送。
  若第一名已经记忆，也不发送，不会退而选择票数第二的类别。

中心区域由 `config/vision.yaml` 配置：

```yaml
workflow:
  team_b_center_width_ratio: 0.40
```

`0.40` 表示只接受画面横向中间40%的范围，即大约 `30%～70%`。该参数只影响
B组豆子阶段，不影响B组数字识别，也不影响A组。允许范围为 `0.05～1.00`，程序
会自动限制越界配置。

以下“位置 -> 数字”的5字节数组只用于B组。A组会将其反向查询为数字1、2、3
各自所在的位置。例如画面从左到右
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
[数字位置] place1~place5上分别识别到什么数字
[豆子结果] 豆子名称、发送码、对应数字、该数字位于第几个位置
[A组豆子位置] 按黄豆、绿豆、白芸豆固定顺序显示三个物理位置
[A组最终结果] 显示最终6字节DATA和每个字节含义
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

### 关闭程序后的断点续跑

只关闭或断开相机、但不退出程序时，工作流状态仍保存在内存中：例如A组豆子已经
识别完成，重新打开相机后会继续等待数字。若使用 `q`、`ESC` 或强制停止使程序
完全退出，内存会消失，因此程序还会把已完成阶段写入磁盘：

```yaml
workflow:
  resume_progress: true
  progress_file: "runtime/workflow_progress.txt"
```

断点在A组豆子阶段完成、各发送阶段生成结果后保存。默认进度文件位于项目根目录下的
`runtime/workflow_progress.txt`。A组新流程使用断点`version 3`；发现旧A组version 1/2
断点时会从头开始，防止把“先数字后豆子”的旧数据误恢复到新流程。

A组示例：

```text
识别3个豆子并保存黄/绿/白各自位置
    -> 保存 waiting_digits（此时不发送）
    -> 关闭程序/相机
    -> 再次启动
    -> 自动恢复 waiting_digits
    -> 识别5个数字并一次发送6字节最终位置结果
```

B组也按相同方式保存阶段：第一个中心豆子发送后恢复到 `waiting_digits`；5个数字
发送后恢复到 `waiting_remaining_beans`，之后继续识别尚未记录的中心豆子。原来的
比赛顺序仍然保留，避免取消顺序后重复发送或把豆子发到错误阶段。

正常恢复时，启动日志应出现类似内容：

```text
[WorkflowResume] 已恢复: mode=team_a, stage=waiting_digits, 豆子=3/3, 箱位=0/5
```

开始一场全新的比赛时必须清除上一场记录。在图像窗口激活时按 `R`，程序会同时
清空内存和进度文件；若关闭了图像窗口，也可以在程序停止后执行：

```bash
rm -f ~/zst/runtime/workflow_progress.txt
```

注意：`serial.simulated: true`时没有真实C板，程序只打印完整发送帧，不访问串口；
无ACK状态机会正常继续并保存断点。
正式比赛前请按`R`或删除进度文件，并把`simulated`改为`false`。

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

NUC目录示例（用户名和安装位置可以任意）：

```text
/任意目录/
  zst/                              # 本项目
  onnxruntime-linux-x64-版本号/       # ONNX Runtime根目录
    include/onnxruntime_cxx_api.h
    lib/libonnxruntime.so
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

推荐在NUC首次配置时明确指定两个依赖根目录。路径只保存在build的CMake缓存中，
不会写死进源码：

```bash
cmake .. \
  -DONNXRUNTIME_ROOT=/你的/onnxruntime-linux-x64-1.27.0 \
  -DMINDVISION_ROOT=/你的/linuxSDK_V2.1.0.49202602041120
```

也可以使用环境变量：

```bash
export ONNXRUNTIME_ROOT=/实际路径/onnxruntime-linux-x64-版本号
export MINDVISION_ROOT=/实际路径/linuxSDK_版本号
cmake ..
```

源码中的项目头文件全部使用相对包含，例如：

```cpp
#include "ImgProcessing/VisionSystem.h"
#include "Communication/VirtualSerial.h"
#include <onnxruntime_cxx_api.h>
#include <CameraApi.h>
```

如果曾用旧的绝对路径生成过build目录，必须清理旧CMake缓存后重新配置：

```bash
cd /你的/zst
rm -rf build
mkdir build && cd build
cmake .. \
  -DONNXRUNTIME_ROOT=/实际路径/onnxruntime根目录 \
  -DMINDVISION_ROOT=/实际路径/MindVision-SDK根目录
make -j"$(nproc)"
```

运行示例：

```bash
./logistics_vision ../config/vision.yaml
```
