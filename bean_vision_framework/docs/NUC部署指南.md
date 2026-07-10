# NUC部署指南

## 1. 前置条件

NUC 需要为 `x86_64`，推荐 Ubuntu 20.04 或 22.04。

安装：

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libopencv-dev
```

如果使用工业相机，还需要在 NUC 上准备 MindVision Linux SDK。

## 2. 推荐部署方式

推荐把整个项目拷到 NUC 后重新编译：

```bash
cd /home/newmaker-11/lzy/new_staff_train_newmaker26/bean_vision_framework
mkdir -p build
cd build

cmake .. \
-DMINDVISION_ROOT=/home/newmaker-11/lzy/new_staff_train_newmaker26/thirdpart/linuxSDK_V2.1.0.37 \
-DONNXRUNTIME_ROOT=../../thirdpart/onnxruntime-linux-x64-1.15.1

cmake --build . -j$(nproc)
```

说明：

- `MINDVISION_ROOT` 必须指向 Linux SDK 根目录
- 该目录下应至少存在：
  - `include/CameraApi.h`
  - `lib/x64/libMVSDK.so`
- 如果你把 SDK 放到这些常见位置，CMake 也会自动搜索，无需再手写参数：
  - `./camera/linuxSDK_V2.x.x`
  - `../linuxSDK_V2.x.x`
  - `../camera/linuxSDK_V2.x.x`

如果 NUC 上 SDK 放在别的位置，只改这个参数，不要改源码里的绝对路径。

## 3. 运行前检查

### 检查依赖库

```bash
ldd ./bean_vision_framework | grep "not found"
```

### 检查 RPATH

```bash
readelf -d ./bean_vision_framework | grep -E "RPATH|RUNPATH"
```

### 检查 OpenCV

```bash
pkg-config --modversion opencv4
```

### 检查 MindVision SDK

```bash
ls /你的SDK目录/include/CameraApi.h
ls /你的SDK目录/lib/x64/libMVSDK.so
```

## 4. 常见问题

### `libonnxruntime.so.1 not found`

处理：

- 确认 `build/` 下有 `libonnxruntime.so*`
- 或确认 RPATH 能指到 `bisai/onnxruntime-linux-x64-1.27.0/lib`

### `libopencv_core.so.xxx not found`

处理：

- 安装 `libopencv-dev`
- 若版本不一致，直接在 NUC 上重新编译

### `YOLO model not found`

处理：

- 检查配置里的 `model_path`
- 确认 `best.onnx` 和 `best.onnx.data` 同目录

### `Serial open failed`

处理：

- 检查串口设备名
- 调试阶段先用 `mock: true`

### `CameraApi.h: No such file or directory`

处理：

- 说明 `MINDVISION_ROOT` 没配对
- 确认 SDK 根目录下有 `include/CameraApi.h`
- 重新执行 `cmake .. -DMINDVISION_ROOT=...`

说明：

- 这会影响工业相机相关目标
- 但纯图片模式和串口 demo 不应因此无法配置

### `libMVSDK.so: cannot open shared object file`

处理：

- 确认构建后 `build/` 目录下已经复制出 `libMVSDK.so`
- 或确认 SDK 的 `lib/x64/libMVSDK.so` 存在
- 必要时检查：

```bash
ldd ./camera_preview_demo | grep MVSDK
```

## 5. 建议测试顺序

1. 先跑 `debug_command_image`
2. 再跑 `camera_preview_demo`
3. 再跑 `debug_camera_mock`
4. 再跑 `serial_protocol_demo`
5. 最后跑 `real_robot`
