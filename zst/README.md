# 起重机物流视觉工程

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
数字键 `1`/`2` 切换；切换会清空旧识别缓存并让 session 自动加一。

线路帧升级为可变长度格式：

```text
A6 VERSION TEAM TYPE SESSION SEQUENCE LENGTH DATA... CRC_L CRC_H
```

串口参数仍为 `115200-8-N-1`，CRC 使用 Modbus CRC16（初值 `0xFFFF`、
多项式 `0xA001`、低字节先发送）。`simulated: true` 时只打印帧，不访问设备。

- `TEAM`：`01=TeamA`，`02=TeamB`。
- `TYPE`：`10=A组数字完成`、`11=A组豆子完成`、`20=B组豆子码`、
  `21=B组数字位置数组`、`30=A组最终完整结果`。
- `SESSION`：区分不同比赛/切换后的任务。
- `SEQUENCE`：本会话消息序号，从 1 递增。
- `LENGTH`：只表示 `DATA` 的字节数。
- 所有 DATA 仍为 `uint8_t`，不使用 ByteConverter。

TeamA 消息顺序：

```text
DigitsComplete：5字节，boxA~boxE 上的数字
BeansComplete：3字节，bean1~bean3 的豆子类型
FinalResult：11字节 = 3豆子 + 5数字 + 每个豆子对应数字所在的3个箱位
```

假设 session=1、数字位置依次为 1/2/3/4/5，第一条数字完成帧为：

```text
A6 01 01 10 01 01 05 01 02 03 04 05 A8 D0
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

假设 `session=1`，实际顺序为“绿豆 -> 数字 -> 黄豆 -> 白芸豆”，一次完整发送为：

```text
绿豆： A6 01 02 20 01 01 01 02 8A D1
数字： A6 01 02 21 01 02 05 04 01 02 05 03 2E 58
黄豆： A6 01 02 20 01 03 01 01 6B 10
白芸豆：A6 01 02 20 01 04 01 03 5B 10
```

## 终端日志

日志只在稳定投票提交、状态切换和串口发送时输出，不会每帧打印 YOLO 框，
避免几十帧每秒刷屏。主要日志包括：

```text
[B组投票] 当前阶段是否新增稳定结果
[数字位置] place1~place5上分别识别到什么数字
[豆子结果] 豆子名称、发送码、对应数字、该数字位于第几个位置
[发送准备] TEAM、TYPE、SESSION、SEQUENCE和十进制DATA
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
