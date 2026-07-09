# CLAUDE.md

## 项目目标

这个工程用于在 Ubuntu 虚拟机中使用 C++ 调用 YOLO 导出的 ONNX 模型，对图片、图片文件夹或视频进行推理，识别豆子和数字目标，并输出检测框、类别名和置信度。

必须使用 ONNX Runtime 推理，不要使用 OpenCV DNN 加载 ONNX。OpenCV 4.5.5 对当前 YOLO11 导出的 ONNX 支持不好，容易报 `kernel_size not specified`。

## 模型文件

模型文件必须同时存在：

```text
best.onnx
best.onnx.data
````

这两个文件必须放在同一个目录。代码只传入 `best.onnx` 路径，但 `best.onnx.data` 不能缺。

## 类别顺序

类别顺序必须和训练时完全一致：

```
0 soybean
1 mung_bean
2 white_kidney_bean
3 data_1
4 data_2
5 data_3
6 data_4
7 data_5
```

不要把数字类别写成 `"1" "2" "3" "4" "5"`，统一使用：

```
const std::vector<std::string> CLASS_NAMES = {
    "soybean",
    "mung_bean",
    "white_kidney_bean",
    "data_1",
    "data_2",
    "data_3",
    "data_4",
    "data_5"
};
```

统计豆子和数字时：

```
if (det.classId >= 0 && det.classId <= 2) {
    ++beanCount;
} else if (det.classId >= 3 && det.classId <= 7) {
    ++numberCount;
}
```

不要只把 `classId == 0` 当作豆子。

## 输入预处理

必须和 YOLO 训练/导出时一致：

1. 输入尺寸固定为 `640x640`
2. 使用 letterbox 等比例缩放
3. padding 颜色为 `(114, 114, 114)`
4. BGR 转 RGB
5. 归一化到 `0.0 - 1.0`
6. 数据格式为 `NCHW`
7. 输入 shape 为：

```
{1, 3, 640, 640}
```

letterbox 的 `scale`、`pad_x`、`pad_y` 必须保存，用于把检测框还原回原图坐标。

## 输出解析

当前模型输出通常是：

```
1 x 12 x 8400
```

其中：

```
4 个框参数 + 8 个类别分数 = 12
```

不要额外乘 objectness。YOLO11 导出的这个输出中，类别分数本身就是最终类别置信度。

解析逻辑：

```
cx = output[0]
cy = output[1]
w  = output[2]
h  = output[3]
class scores = output[4] 到 output[11]
```

对每个 anchor 取最高类别分数：

```
bestScore = max(class_scores)
bestClass = argmax(class_scores)
```

如果 `bestScore < conf_threshold`，跳过。

框还原：

```
x1 = (cx - w / 2 - pad_x) / scale
y1 = (cy - h / 2 - pad_y) / scale
x2 = (cx + w / 2 - pad_x) / scale
y2 = (cy + h / 2 - pad_y) / scale
```

最后 clamp 到原图范围内。

## NMS

使用 NMS 去重，推荐阈值：

```
conf_threshold = 0.25
nms_threshold = 0.45
```

如果虚拟机里检测不到数字，可以临时把置信度降到：

```
conf_threshold = 0.20
```

数字目标比豆子难，阈值过高会漏检。

## 翻转问题

Windows 实测时相机画面曾出现镜像问题。虚拟机里如果画面左右反了，应增加可选参数 `flip_mode`：

```
0 不翻转
1 左右翻转
2 上下翻转
3 上下左右翻转
```

翻转必须在推理之前做，这样显示、保存、检测框都会一致。

示例：

```
if (flip_mode == 1) {
    cv::flip(frame, frame, 1);
} else if (flip_mode == 2) {
    cv::flip(frame, frame, 0);
} else if (flip_mode == 3) {
    cv::flip(frame, frame, -1);
}
```

## 程序参数建议

建议程序支持：

```
参数1：best.onnx 路径
参数2：输入路径，可以是图片、图片文件夹、视频
参数3：输出目录
参数4：置信度阈值，默认 0.25
参数5：NMS 阈值，默认 0.45
参数6：flip_mode，默认 0
```

示例：

```
./bean_number_detector ./model/best.onnx ./frames ./output 0.25 0.45 0
```

如果画面左右反：

```
./bean_number_detector ./model/best.onnx ./frames ./output 0.25 0.45 1
```

## 输出要求

程序至少要输出：

1. 终端打印每个目标：

```
source=xxx.jpg class=data_3 confidence=0.82 box=[x,y,w,h]
```

2. 保存带检测框的图片到输出目录：

```
output/000001.jpg
output/000002.jpg
...
```

3. 如果输入是视频，建议同时保存带检测框的视频：

```
output/result.avi
```

## CMake 要求

必须链接：

```
OpenCV
ONNX Runtime
```

不要使用 OpenCV DNN 加载模型。

示例结构：

```
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs videoio highgui)

set(ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/onnxruntime-sdk")
set(ONNXRUNTIME_INCLUDE_DIRS "${ONNXRUNTIME_ROOT}/include")
set(ONNXRUNTIME_LIBRARIES "${ONNXRUNTIME_ROOT}/lib/libonnxruntime.so")

add_executable(bean_number_detector main.cpp)

target_include_directories(bean_number_detector PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${ONNXRUNTIME_INCLUDE_DIRS}
)

target_link_libraries(bean_number_detector PRIVATE
    ${OpenCV_LIBS}
    ${ONNXRUNTIME_LIBRARIES}
)

set_target_properties(bean_number_detector PROPERTIES
    BUILD_RPATH "${ONNXRUNTIME_ROOT}/lib"
)
```

## 常见问题排查

如果模型加载失败：

```
检查 best.onnx 和 best.onnx.data 是否在同一目录
检查 ONNX Runtime 路径是否正确
不要用 OpenCV DNN 加载 ONNX
```

如果检测框位置不对：

```
检查 letterbox 的 pad_x、pad_y、scale 是否正确
检查输出框是否按 cx, cy, w, h 解析
检查是否在翻转前后混用了坐标
```

如果类别错乱：

```
检查 CLASS_NAMES 顺序
检查类别数是否为 8
检查输出 shape 是否为 1x12x8400
```

如果豆子统计错：

```
0,1,2 都是豆子
3,4,5,6,7 都是数字
```

如果数字漏检严重：

```
先把 conf_threshold 降到 0.20 测试
检查画面是否太暗、太糊、太远
检查是否需要 flip_mode=1
```

## 当前建议

优先修正现有虚拟机工程中的：

1. 类别名
2. 豆子/数字统计逻辑
3. 参数化 conf 和 nms
4. 加入 flip_mode
5. 保存检测后的图片，方便和 Windows 结果对比

```

这份 `CLAUDE.md` 的核心作用就是提醒虚拟机里的 Codex/Claude：**不要用 OpenCV DNN、类别顺序不能错、预处理必须一致、必要时加翻转参数**。
```