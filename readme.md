# 校内赛：Bean_Vision

基于 YOLO + ONNX Runtime 的豆类识别与路径引导系统。相机采集 → 模型推理 → 稳定跟踪 → 查表 → 串口发送指令给云台。

---

## 项目结构

```
├── config/
│   ├── vision_config.yaml    # 视觉参数（热重载）
│   └── route_config.txt      # 路径映射表（热重载）
├── include/
│   ├── config.hpp            # VisionConfig 结构体 + 颜色表声明
│   ├── cemare.hpp            # 迈德威视相机封装
│   ├── detection.hpp         # 检测结果结构体 + 预处理/后处理声明
│   ├── yolo_detector.hpp     # YOLO 检测器（ONNX Runtime 推理）
│   ├── stable_tracker.hpp    # 稳定跟踪状态机
│   ├── route_config.hpp      # 路径映射 + 文件工具
│   ├── visualizer.hpp        # 调试可视化声明
│   ├── serial.hpp            # 串口通信声明
│   ├── packet.hpp            # 串口数据包定义
│   └── CRC16.hpp             # CRC16 校验声明
├── src/
│   ├── vision.cpp            # 主程序（函数式编排）
│   ├── config.cpp            # YAML 配置加载 + 颜色表生成
│   ├── camera.cpp            # 相机驱动实现
│   ├── datection.cpp         # 预处理（letterbox + CLAHE）+ 后处理（NMS）
│   ├── stable_tracker.cpp    # 跟踪状态机实现
│   ├── visualizer.cpp        # 调试画面绘制
│   ├── serial.cpp            # Linux termios 串口实现
│   └── CRC16.cpp             # CRC16 实现
```

---

## 参数文件

### vision_config.yaml

```yaml
detection:
  confidence_threshold: 0.5   # 置信度阈值
  nms_threshold: 0.25         # NMS IoU 阈值
  input_width: 640            # 模型输入宽
  input_height: 640           # 模型输入高
  used_classes: 3             # 只用前N类，-1=全部

display:
  font_scale: 0.6             # 文字大小
  font_thickness: 2           # 文字粗细
  line_thickness: 2           # 检测框粗细
```

### route_config.txt

每行规则：`类别名 first_cmd second_cmd turn_strength`
```txt
soybean 0 1 30
mung_bean 1 2 50
white_kidney_bean 2 3 60
```
文件修改后自动重载，`RouteConfig::fileChanged()` 检测。

---

## 加载参数

### config.hpp 
可调参数结构体VisionConfig:成员包含yaml文件所有参数类型及默认值。

### config.cpp

| 函数 | 功能 |
|------|------|
| `loadVisionConfig` | 用 yaml-cpp 分级读取，字段不存在用默认值 |
| `buildColorTable` | 颜色表，生成 N 种 HSV 色相均匀分布 |

### route_config.hpp

| 组件 | 说明 |
|------|------|
| `PathCommand` | 参数结构体，成员包含txt文件所有参数类型及默认值。|
| `RouteConfig` | 路径配置类 |

#### RouteConfig 类

| 方法 | 功能 |
|------|------|
| `load` | fstream读取，存入mapping_，失败用默认值 |
| `lookup` | mapping_.find(name)查表，返回 `optional<PathCommand>` |
| `fileChanged` | 调用safeLastWriteTime获取文件修改时间进行比较，用于热重载 |

| 成员 | 功能 |
|------|------|
| `filepath_` | 参数文件路径 |
| `mapping_` | 参数存放容器 |
| `mtime_` | 文件最后修改时间 |

### 工具函数（`utils.hpp` ）

| 方法 | 功能 |
|------|------|
| `safeLastWriteTime` | 工具函数，使用last_write_time获取文件修改时间，文件不存在返回零值 |
| `resolveProjectPath` | 工具函数，fs::exists遍历搜索路径 |

---

## 相机模块（`cemare.hpp` / `camera.cpp`）

迈德威视工业相机 SDK 封装。

| 方法 | 功能 |
|------|------|
| `构造函数` | 初始化sdk |
| `析构函数` | 调用close() |
| `open(w, h)` | 枚举 → 相机初始化 → 设置 ISP 格式（自动判断彩色/黑白） → cameraplay |
| `getFrame()` | 阻塞取一帧，2000ms 超时，经 SDK 图像处理后返回 `cv::Mat`（BGR） |
| `close()` | cameraUninit->释放缓存 |

分辨率不强制设置，相机跑原生尺寸，`preprocess` 里 letterbox 做缩放。

---

## YOLO 检测器（`yolo_detector.hpp`）

封装 ONNX Runtime 推理全流程。由于 `Ort::Session` 是模板类，全部实现放在头文件。

| 方法 | 功能 |
|------|------|
| `loadModel(path, w, h)` | 加载 .onnx → 读输入形状 → `getOutputInfo()` 算类别数/锚点数 → 预分配 tensor |
| `infer(frame, threshold)` | 预处理 → 创建 tensor → `session_.Run()` 推理 → `parseYOLOv8Output*` 后处理 |
| `setNmsThreshold(t)` | 设置 NMS IoU 阈值 |
| `setUsedClasses(n)` | 设置只用前N类（-1=全部） |

### 预处理（`datection.cpp`）

原图 → CLAHE 直方图均衡化（Lab色彩空间，仅L通道） → Letterbox → RGB → 归一化 → NCHW blob

| 函数 | 说明 |
|------|------|
| `applyCLAHE` | [classes+4, anchors] 格式 |
| `parseYOLOv8OutputAnchorFirst` | [anchors, classes+4] 格式|
| `applyNMS` | 按置信度排序 → IoU 过滤 → 去重 |
| `getOutputInfo` | 从输出 shape 自动推断 `numClasses` 和 `numAnchors` |

### 后处理（`datection.cpp`）

| 函数 | 说明 |
|------|------|
| `parseYOLOv8OutputFeatureFirst` | [classes+4, anchors] 格式 |
| `parseYOLOv8OutputAnchorFirst` | [anchors, classes+4] 格式|
| `applyNMS` | 按置信度排序 → IoU 过滤 → 去重 |
| `getOutputInfo` | 从输出 shape 自动推断 `numClasses` 和 `numAnchors` |

---

## 稳定跟踪（`stable_tracker.hpp` / `.cpp`）

状态机抗误触发：

```
检测到目标 → 连续 N 帧一致 → 确认输出 → 进入冷却期（发送后沉默 M 帧） → 期满解锁
```

| 方法 | 功能 |
|------|------|
| `update(dets, classNames)` | 取最高置信度目标，计数/切换/冷却逻辑，确认时返回目标名 |
| `reset()` | 重置当前计数（不清除冷却状态） |

冷却期保护：同一目标触发后需等冷却期满才能再次触发，避免重复发送。

---

## 串口通讯

### packet.hpp —— 数据包

```cpp
struct VisionSendPacket {
    uint8_t  header   = 0x5A;     // 帧头
    uint16_t first_cmd      : 2;  // bit 0-1（0=直行/1=左转/2=右转）
    uint16_t second_cmd     : 2;  // bit 2-3（1=左/2=中/3=右分支）
    uint16_t turn_strength  : 7;  // bit 4-10（0~120）
    uint16_t reserved       : 5;  // bit 11-15
    uint16_t checksum;            // CRC16占位
} __attribute__((packed));       // 5 字节紧凑布局
```

使用`bit-filed`（位域）直接读写字段，`static_assert`（静态断言）编译时校验布局。

| 函数 | 功能 |
|------|------|
| `makePacket` | 构造数据包结构体 |
| `toVector(packet)` | 结构体转成字节数组（NUC发送） |
| `fromVector(data)` | 字节数组转成结构体（云台接收） |
| `pack` | crc16(Append_CRC16_Check_Sum),序列化(toVector) |

### serial.cpp —— Linux termios 串口

| 方法 | 功能 |
|------|------|
| `构造函数` | fd_初始赋值-1 |
| `析构函数` | 调用close() |
| `open("/dev/gimbal")` | 打开串口，调用configurePort进行配置，tcflush清理垃圾数据 |
| `configurePort()` | 调用termios结构体配置模式，tcsetattr(fd_, TCSANOW, &tty)将模式配置到fd对应的串口|
| `send(data, len)` | 使用write写入字节data之后len长度的内容到串口 |
| `transmit` | 重试，重连，满足条件执行发送返回true直接结束函数|
| `tryReconnect` | 关闭串口，睡眠，打开串口 |
| `close` | 关闭串口 |
| `isOpen()` | return fd_>=0 |

| 成员 | 功能 |
|------|------|
| `int fd_` | 文件描述符，操作系统内核用来标识“打开的文件/设备”的一串数字编号 |
| `std::string portName_` | 设备路径 |
| `bool txLogEnabled_` | 是否打印日志 |
| `bool autoReconnect_ ` | 是否自动重连 |
| `std::chrono::steady_clock::time_point lastReconnectAttempt_{}` | 最后一次重连的时间 |
| `int  reconnectFailCount_` | 重连失败次数，失败一次加一 |
| `int  reconnectCooldownMs_` | 重连冷却时间，由yaml设置 |
| `int  maxReconnectAttempts_` | 最大重连次数，有yaml设置 |

### CRC16.hpp —— 校验
constexpr uint16_t CRC16_INIT = 0xFFFF;
| 函数 | 功能 |
|------|------|
| `Get_CRC16_Check_Sum` | 位运算，查表 |
| `Append_CRC16_Check_Sum(buf, len)` | 调用Get_CRC16_Check_Sum计算 CRC 填入末尾两字节 |
| `Verify_CRC16_Check_Sum(buf, len)` | 重新计算 CRC 并与包尾比对，验证完整性 |

---

## 可视化（`visualizer.cpp`）

| 函数 | 功能 |
|------|------|
| `buildColorTable` | 颜色表，类别比例*180，颜色在hsv空间均匀分布 |
| `initDebugWindow` | 可视化窗口并修改初始大小 |
| `drawDebug` |  |

`drawDebug()` 在帧上叠加三层信息：

1. **检测框**：矩形 + 标签（类别名、置信度、坐标尺寸），颜色按类别编码
2. **四角标记**：红色圆点，确认画面正常渲染
3. **状态栏**：YOLO/串口灯号、FPS、跟踪状态（stable counter/cool down）、检测数量

---

## 主程序（`vision.cpp`）

函数式架构，8 个函数各司其职：

```
main()
 ├── loadAllConfigs()           → (VisionConfig, RouteConfig, mtime)
 ├── buildColorTable()          → 颜色表
 ├── initDetector()             → bool（模型可降级）
 ├── initCamera()               → Camera（失败抛异常退出）
 ├── initSerial()               → bool（串口可降级）
 └── runLoop()                  → 主循环永不返回
       ├── reloadRoutesIfChanged()      → RouteConfig
       ├── reloadVisionIfChanged()      → {VisionConfig, mtime, changed}
       ├── captureFrame()               → optional<Mat>（带自动重连）
       ├── runInference()               → vector<Detection>
       ├── printDiagnostics()           → 每30帧输出终端日志
       ├── tracker.update() → routes.lookup() → sendCommand()
       │     └── 封包 → CRC → serial.send()（或模拟打印）
       └── renderFrame()                → bool（按q退出）
```

**容错设计**：模型和串口失败不退出，照常显示相机画面。相机失败直接退出。

---

## 构建与运行

```bash
cd build
cmake ..
make -j$(nproc)
./bean_vision
```

依赖：OpenCV 4.x、yaml-cpp、ONNX Runtime、MindVision SDK。
