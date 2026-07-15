# camera_preview_demo 相机调参与 YOLO 预览使用说明

本文档只说明当前本地代码已经实现的 `camera_preview_demo` 行为。

核对依据：

- [tools/camera_preview_demo/main.cpp](/home/ygk/yolo_competition/bean_vision_framework/tools/camera_preview_demo/main.cpp)
- [src/input/InputManager.cpp](/home/ygk/yolo_competition/bean_vision_framework/src/input/InputManager.cpp)
- [src/camera/CameraManager.cpp](/home/ygk/yolo_competition/bean_vision_framework/src/camera/CameraManager.cpp)
- [src/core/AppConfig.cpp](/home/ygk/yolo_competition/bean_vision_framework/src/core/AppConfig.cpp)
- [include/core/AppConfig.h](/home/ygk/yolo_competition/bean_vision_framework/include/core/AppConfig.h)
- [config/camera_preview_demo.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/camera_preview_demo.yaml)
- [config/camera_yolo_preview.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/camera_yolo_preview.yaml)
- [docs/CONFIG_REFERENCE.md](/home/ygk/yolo_competition/bean_vision_framework/docs/CONFIG_REFERENCE.md)

## 一、工具定位

`camera_preview_demo` 是一个独立调试工具，主要用于：

- 打开普通相机或 MindVision 工业相机；
- 实时显示画面；
- 可选显示 ROI；
- 可选运行 YOLO；
- 显示检测框、类别和置信度；
- 用于寻找相机角度、曝光、增益和 ROI；
- 可以保存当前调试画面。

它不经过以下业务链路：

- `TaskStateMachine`
- `MultiFrameRecognition`
- `DigitInference`
- `TaskGenerator`
- `Protocol`
- `SerialPort`
- `STM32`

因此它是独立调试工具，不代表完整上车业务闭环。

## 二、构建和运行方式

从项目根目录构建：

```bash
cd /home/ygk/yolo_competition/bean_vision_framework
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc) --target camera_preview_demo
```

运行方式一：在 `build` 目录运行

```bash
cd /home/ygk/yolo_competition/bean_vision_framework/build
./camera_preview_demo ../config/camera_preview_demo.yaml
./camera_preview_demo ../config/camera_yolo_preview.yaml
```

运行方式二：在项目根目录运行

```bash
cd /home/ygk/yolo_competition/bean_vision_framework
./build/camera_preview_demo ./config/camera_preview_demo.yaml
./build/camera_preview_demo ./config/camera_yolo_preview.yaml
```

查看当前源码提供的简要帮助：

```bash
./camera_preview_demo --help
```

当前源码中的帮助会打印：

- 用法：`Usage: <app> <config_path>`
- 按键：`s=save current frame, r=reload config/roi, q=quit`

## 三、配置文件用途

当前常用配置文件：

- [config/camera_preview_demo.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/camera_preview_demo.yaml)
  - 主要用于相机画面、ROI 和基础相机调参。
- [config/camera_yolo_preview.yaml](/home/ygk/yolo_competition/bean_vision_framework/config/camera_yolo_preview.yaml)
  - 在相机预览基础上启用 YOLO 检测和性能观察。

说明：

- 应以当前两个 YAML 的真实字段为准。
- 不要在使用时自行假定存在尚未实现的参数。
- `camera_preview_demo.yaml` 当前以 `mindvision_camera` 为输入，并开启窗口、鼠标坐标显示与 ROI 调试。
- `camera_yolo_preview.yaml` 当前在 `mindvision_camera` 基础上启用 `onnxruntime` YOLO，线程配置为 `intra_op_threads: 2`，并开启性能日志。

性能日志参数的详细定义，以 [docs/CONFIG_REFERENCE.md](/home/ygk/yolo_competition/bean_vision_framework/docs/CONFIG_REFERENCE.md) 为准。

## 四、键盘操作

| 按键 | 功能 |
|---|---|
| `s` / `S` | 保存当前显示画面 |
| `r` / `R` | 重新读取启动时指定的配置文件和 ROI |
| `q` / `Q` | 正常退出 |
| `Esc` | 正常退出 |

补充说明：

- OpenCV 通过 `cv::waitKey()` 接收按键。
- 使用按键前应先点击相机预览窗口，使窗口获得键盘焦点。
- 如果使用中文输入法，建议切换到英文输入状态。
- 如果按 `s` 后终端完全没有输出，优先检查预览窗口是否获得焦点。
- 推荐使用 `q` 或 `Esc` 正常退出，让程序执行 `input.release()` 释放相机资源。

## 五、s 保存画面的真实行为

当前代码中，按 `s` 保存的是 `visual` 显示画面，不是原始相机 `frame`。

因此保存图片可能包含：

- ROI 框和标签；
- YOLO 检测框；
- 类别和置信度；
- 鼠标坐标标记。

还要注意：

- `s` 只保存图片。
- `s` 不会保存相机曝光、增益、白平衡等参数。
- 当前代码没有把相机参数自动写回 YAML 的快捷键。
- 当前也没有“保存相机参数”的独立按键。

保存日志：

成功时：

```text
[SAVE] <保存路径>
```

失败时：

```text
[WARN] failed to save <保存路径>
```

## 六、保存目录规则

这一点需要特别注意。

当前 `debug.output_dir` 如果是相对路径，它是相对于程序启动时的当前工作目录 `pwd`，而不是：

- 相对于 YAML 文件目录；
- 固定相对于项目根目录；
- 固定相对于可执行文件目录。

例如，YAML 中当前写的是：

```yaml
debug:
  output_dir: "debug_output/camera_preview"
```

如果这样运行：

```bash
cd /home/ygk/yolo_competition/bean_vision_framework/build
./camera_preview_demo ../config/camera_yolo_preview.yaml
```

实际保存目录是：

```text
/home/ygk/yolo_competition/bean_vision_framework/build/debug_output/camera_preview/
```

如果这样运行：

```bash
cd /home/ygk/yolo_competition/bean_vision_framework
./build/camera_preview_demo ./config/camera_yolo_preview.yaml
```

实际保存目录是：

```text
/home/ygk/yolo_competition/bean_vision_framework/debug_output/camera_preview/
```

建议用下面两条命令确认当前工作目录和最近保存结果：

```bash
pwd
ls -lht debug_output/camera_preview | head
```

文件名规则：

- 文件名依次为 `0001_preview.png`、`0002_preview.png`。
- 保存编号在每次程序启动时从 `0001` 开始。
- 如果目录中已有同名文件，可能被覆盖。
- 应以终端打印的 `[SAVE]` 路径为最终依据。

## 七、r 重新加载的实际范围

按 `r` 后，当前代码会重新读取原来的 `config_path`，并打印：

```text
[RELOAD] <配置路径>
```

以及一条提醒：

```text
[RELOAD] restart demo to apply yolo_enable/model changes
```

### 可以在下一帧直接反映的内容

主要包括：

- ROI 文件和 ROI 坐标；
- `preview.draw_roi`；
- `preview.draw_boxes`；
- `preview.print_detections`；
- `debug.show_mouse_position`。

### 必须退出并重新启动程序才能可靠应用的内容

主要包括：

- `input.type`；
- `input.source`；
- `camera_id`；
- 自动曝光；
- 曝光时间；
- 自动增益；
- 增益；
- 白平衡；
- 图像翻转；
- 图像旋转；
- YOLO 是否启用；
- YOLO 模型路径；
- `conf_threshold`；
- `nms_threshold`；
- `intra_op_threads`；
- `performance_logging`；
- `performance_log_interval`；
- `performance_window_size`；
- 其他需要重新构造 `BeanNumberDetector` 或 `ORT Session` 的配置。

原因是：

- `InputManager` 在启动时构造；
- `CameraManager` 在启动时打开相机；
- `BeanNumberDetector` 在启动时构造；
- `ONNX Runtime Session` 在模型加载时创建；
- `r` 只重新读取 `AppConfig`，不会重建以上对象。

因此不要把 `r` 理解成“所有配置热更新”。

另外还要注意：

- 如果运行中修改 `debug.output_dir` 后只按 `r`，新目录不一定会重新创建。
- 修改保存目录后建议退出并重新启动。

## 八、相机参数何时生效

根据当前 [src/camera/CameraManager.cpp](/home/ygk/yolo_competition/bean_vision_framework/src/camera/CameraManager.cpp)，MindVision 相机参数的应用时机如下。

### MindVision 相机路径

- `auto_exposure` 在打开相机时通过 `CameraSetAeState()` 应用；
- `auto_exposure=false` 时，`exposure_time` 通过 `CameraSetExposureTime()` 应用；
- `auto_gain=false` 时，`gain` 通过 `CameraSetAnalogGainX()` 应用；
- `auto_white_balance` 通过 `CameraSetWbMode()` 应用；
- `flip_horizontal`、`flip_vertical` 和 `rotate` 是取图后的图像处理配置。

必须说明：

- 上述参数保存在 `CameraManager` 创建时取得的配置副本中；
- 修改 YAML 后只按 `r` 不会更新已打开相机；
- 正确操作是退出程序并重新启动。

### OpenCV 普通相机路径

根据 [src/input/InputManager.cpp](/home/ygk/yolo_competition/bean_vision_framework/src/input/InputManager.cpp)，`camera` 模式会在 `open()` 时设置：

- `CAP_PROP_FRAME_WIDTH`
- `CAP_PROP_FRAME_HEIGHT`
- `CAP_PROP_FPS`
- `CAP_PROP_AUTO_EXPOSURE`
- 手动曝光时的 `CAP_PROP_EXPOSURE`
- `CAP_PROP_GAIN`

### 关于 width / height / fps 的真实边界

- `width`、`height`、`fps` 在 OpenCV `camera` 路径中有设置。
- 当前 MindVision 路径没有看到对应的 SDK 分辨率和帧率设置调用。
- 因此不要把 `width`、`height`、`fps` 描述成在 MindVision 模式下一定生效。

## 九、推荐相机调参流程

推荐现场流程：

1. 选择 `camera_preview_demo.yaml` 或 `camera_yolo_preview.yaml`。
2. 启动预览程序。
3. 观察亮度、拖影、噪声、数字清晰度和 YOLO 稳定性。
4. 按 `s` 保存当前对比画面。
5. 使用 `q` 或 `Esc` 正常退出。
6. 修改 YAML 中的曝光、增益、白平衡或旋转参数。
7. 重新启动程序，使相机参数真正生效。
8. 再次观察并按 `s` 保存画面。
9. 对比不同参数下的识别稳定性。
10. 记录最终采用的 YAML 参数。

调参原则：

- 当前识别精度优先于延迟。
- 调参时先保证数字清晰、检测稳定，再观察推理延迟。
- 不要只凭单帧效果确定最终配置。
- 最终配置需要在真实角度和连续画面下验证。

## 十、YOLO 预览说明

`camera_yolo_preview.yaml` 启用 YOLO 后，可以观察：

- 检测类别；
- 置信度；
- 检测框；
- ROI 与检测框的空间关系；
- `preprocess_ms`；
- `inference_ms`；
- `postprocess_ms`；
- `detect_total_ms`；
- `P50`；
- `P95`。

提醒：

- 性能日志参数详细定义以 [docs/CONFIG_REFERENCE.md](/home/ygk/yolo_competition/bean_vision_framework/docs/CONFIG_REFERENCE.md) 为准。
- `intra_op_threads=2` 只是当前 `NUC` 上已经验证可用的配置。
- 不能把当前测试数据写成所有设备的性能保证。
- 修改线程数后必须重新启动。
- 线程数调优必须以检测结果一致为前提。

## 十一、常见日志说明

| 日志前缀 | 含义 |
|---|---|
| `[PREVIEW]` | 当前预览、ROI、YOLO 和绘制开关 |
| `[YOLO]` | 模型、后端、阈值、线程策略和模型输入输出信息 |
| `[YOLO][PERF]` | 当前性能统计 |
| `[SAVE]` | 图片保存成功及保存路径 |
| `[WARN] failed to save` | 图片保存失败 |
| `[RELOAD]` | 配置文件已重新读取，但不代表相机和 detector 已重建 |
| `[WARN] CameraSetWbMode failed` | 白平衡设置失败警告；如果后续仍出现 `MindVision camera opened`，说明相机总体仍然成功打开；但白平衡配置可能没有按预期应用，应单独检查 |
| `Gtk canberra-gtk-module` 警告 | 通常属于 GTK 可选模块提示；不要把它直接判断成相机或 YOLO 失败 |
| `CXP system count:0` 等设备扫描信息 | 属于 SDK 的设备扫描输出；如果后续 `MindVision camera opened` 成功出现，不能仅凭前面的扫描信息判断相机打开失败 |

说明：

- 不要对未核实的 SDK 状态码作过度解释。

## 十二、常见问题排查

### 1. 按 s 没有任何反应

检查：

- 是否点击了 OpenCV 预览窗口；
- 是否切换到英文输入法；
- 终端有没有 `[SAVE]` 或 `[WARN]`；
- 预览循环是否仍在正常刷新。

### 2. 终端显示 [SAVE]，但找不到图片

检查：

- 当前 `pwd`；
- `debug.output_dir`；
- 是否在 `build/debug_output` 中查找；
- 以 `[SAVE]` 打印的路径为准。

### 3. 修改曝光或增益后按 r 没变化

原因：

- `r` 不会重新创建 `CameraManager`；
- 必须退出并重新启动。

### 4. 修改 ROI 后如何生效

- 保存 ROI YAML；
- 回到预览窗口按 `r`；
- 下一帧检查新 ROI。

### 5. 出现 [WARN] failed to save

检查：

- 输出目录是否存在；
- 当前用户是否有写权限；
- 磁盘空间是否充足；
- 是否按 `r` 切换到了一个尚未创建的新 `output_dir`。

### 6. 图片似乎没有新增

说明：

- 每次启动保存编号从 `0001` 开始；
- 同名文件可能被覆盖；
- 使用 `ls -lht` 检查修改时间。

## 十三、当前没有实现的功能

当前没有实现：

- 键盘实时增加或减少曝光；
- 键盘实时增加或减少增益；
- OpenCV Trackbar 滑条调参；
- 自动将当前参数写回 YAML；
- 保存相机参数快捷键；
- 自动生成不重名的时间戳文件；
- 所有配置统一热更新。

这些可以作为未来优化方向，但不能写成当前已经支持。

## 十四、修改完成后的检查

本 README 编写时已按当前源码逐项核对，结论如下：

- 只修改了 `tools/camera_preview_demo/README.md`；
- 所有按键与 `main.cpp` 一致；
- 保存路径说明与 `AppConfig.cpp` 和当前工作目录规则一致；
- 没有把 `s` 写成保存参数；
- 没有把 `r` 写成完整热更新；
- 没有声称 MindVision 的 `width`、`height`、`fps` 已完全生效；
- Markdown 表格和代码块格式正常；
- 不执行 `Git commit`。
