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
- `TYPE`：`10=数字完成`、`11=豆子完成`、`20=单豆完成`、
  `21=豆子数字匹配`、`30=最终完整结果`。
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
BeanDetected：2字节 = 豆子位置、豆子类型
BeanDigitMatch：4字节 = 豆子位置、豆子类型、对应数字、数字所在箱位
后两个豆子每稳定识别一个，再各发送一次 BeanDigitMatch
```

假设 session=1、第一个豆子位置为黄豆，第一条单豆完成帧为：

```text
A6 01 02 20 01 01 02 01 01 61 97
```

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

目录建议：

```text
/home/zst/Desktop/
  zst/
  onnxruntime-linux-x64-1.27.0/
    onnxruntime-linux-x64-1.27.0/
      include/
      lib/
  linuxSDK_V2.1.0.49202602041120/
    include/
    lib/x64/
```

编译示例：

```bash
mkdir -p build
cd build
cmake ..
make -j
```

如果依赖目录不在桌面同级，手动指定：

```bash
cmake .. \
  -DONNXRUNTIME_ROOT=/你的/onnxruntime-linux-x64-1.27.0 \
  -DMINDVISION_ROOT=/你的/linuxSDK_V2.1.0.49202602041120
```

运行示例：

```bash
./logistics_vision ../config/vision.yaml
```
