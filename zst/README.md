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
