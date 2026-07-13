# 起重机物流视觉工程

第一次阅读本工程时，建议先看：

- [视觉处理与串口通信完整流程说明](视觉处理与串口通信完整流程说明.md)

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

## 两队单向分阶段通信

两个队伍使用不同 C 板但不会同时上场，因此保留一个串口驱动，通过
`config/vision.yaml` 的 `workflow.team_mode` 选择流程。调试窗口中也可以按
数字键 `1`/`2` 切换；切换会清空旧识别缓存。
`team_mode` 只在视觉电脑内部选择状态机，不会作为字节发送给电控。

线路使用固定命令长度的最简格式：

```text
A6 CMD DATA... CRC_L CRC_H
```

串口参数仍为 `115200-8-N-1`，CRC 使用 Modbus CRC16（初值 `0xFFFF`、
多项式 `0xA001`、低字节先发送）。`simulated: true` 时只打印帧，不访问设备。

- `A6`：固定帧头。
- `CMD`：命令码，同时决定后面DATA的固定长度。
- `DATA`：全部为 `uint8_t`，不使用ByteConverter。
- `CRC_L/CRC_H`：覆盖A6、CMD和全部DATA，低字节先发。
- `VERSION、TEAM、SESSION、SEQUENCE、LENGTH` 均不再发送给电控。

固定命令表：

| CMD | 用途 | DATA长度 | 完整帧长度 |
|---|---|---:|---:|
| `0x10` | A组五个位置的数字 | 5 | 9 |
| `0x11` | A组三个豆子的类型 | 3 | 7 |
| `0x20` | B组当前中心豆子码 | 1 | 5 |
| `0x21` | B组五个位置的数字 | 5 | 9 |

TeamA 消息顺序：

```text
1. CMD=0x10，DATA=[place1, place2, place3, place4, place5]
2. CMD=0x11，DATA=[bean1, bean2, bean3]
```

A组不再发送FinalResult，也不发送bean_target_place。示例：

```text
数字：A6 10 01 02 03 04 05 D1 21
豆子：A6 11 01 02 03 39 84
```

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

数字数组采用“位置 -> 该位置上的数字”，不进行反向换算。例如画面从左到右
五个位置识别为 `4 1 2 5 3`，发送的 DATA 就是：

```text
04 01 02 05 03
```

实际顺序为“绿豆 -> 数字 -> 黄豆 -> 白芸豆”时，一次完整发送为：

```text
绿豆： A6 20 02 09 E2
数字： A6 21 04 01 02 05 03 C9 16
黄豆： A6 20 01 49 E3
白芸豆：A6 20 03 C8 22
```

算法端实际生成的数据类型是 `std::vector<uint8_t>`，封帧前内容为
`[CMD][DATA...]`；`VirtualSerial` 再添加帧头 `A6` 和 CRC16 后发送。

## 终端日志

日志只在稳定投票提交、状态切换和串口发送时输出，不会每帧打印 YOLO 框，
避免几十帧每秒刷屏。主要日志包括：

```text
[B组投票] 当前阶段是否新增稳定结果
[数字位置] place1~place5上分别识别到什么数字
[豆子结果] 豆子名称、发送码、对应数字、该数字位于第几个位置
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

当前为视觉单向发送模式，没有 ACK。`write()` 成功只代表 Linux 已把数据交给
串口驱动，不能证明 C 板已经收到或完成动作；后续需要更高可靠性时再增加接收、
ACK、超时重发和 `ControllerReady`。

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

只关闭或断开相机、但不退出程序时，工作流状态仍保存在内存中：例如A组已经发送
数字数组，重新连接相机后会继续等待豆子。若使用 `q`、`ESC` 或强制停止使程序
完全退出，内存会消失，因此程序还会把已经成功发送的阶段写入磁盘：

```yaml
workflow:
  resume_progress: true
  progress_file: "runtime/workflow_progress.txt"
```

断点只在 `VirtualSerial::sendPacket()` 返回成功后保存，不会把“已经识别但串口写入
失败”的结果误记为已经完成。默认进度文件位于项目根目录下的
`runtime/workflow_progress.txt`。

A组示例：

```text
识别并发送5个数字
    -> 保存 waiting_beans
    -> 关闭程序/相机
    -> 再次启动
    -> 自动恢复 waiting_beans
    -> 直接识别并发送3个豆子
```

B组也按相同方式保存阶段：第一个中心豆子发送后恢复到 `waiting_digits`；5个数字
发送后恢复到 `waiting_remaining_beans`，之后继续识别尚未记录的中心豆子。原来的
比赛顺序仍然保留，避免取消顺序后重复发送或把豆子发到错误阶段。

正常恢复时，启动日志应出现类似内容：

```text
[WorkflowResume] 已恢复: mode=team_a, stage=waiting_beans, 豆子=0/3, 箱位=5/5
```

开始一场全新的比赛时必须清除上一场记录。在图像窗口激活时按 `R`，程序会同时
清空内存和进度文件；若关闭了图像窗口，也可以在程序停止后执行：

```bash
rm -f ~/zst/runtime/workflow_progress.txt
```

注意：`serial.simulated: true` 时模拟发送也会返回成功，因此测试过程同样会保存
断点。正式比赛前请按 `R` 或删除进度文件。当前协议没有电控ACK，“发送成功”只
表示Linux已将完整字节帧写入串口；若以后要求确认C板已经处理，需要再增加双向
ACK与超时重发。

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
