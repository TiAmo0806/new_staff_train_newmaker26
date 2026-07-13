# RUN_COMMANDS

## 编译工程

如果本机有 `cmake`：

```bash
cmake -S . -B build
cmake --build build -j4
```

如果仓库已经有现成的 `build/` 目录，也可以直接使用里面的可执行文件。

## 图片 + 真串口模式

直接运行：

```bash
sudo ./build/bean_vision_framework config/debug_image_real_serial.yaml
```

使用脚本：

```bash
sudo ./scripts/run_debug_image_real_serial.sh
```

日志会保存到 `logs/`。

## 真实相机 + 真串口模式

直接运行：

```bash
sudo ./build/bean_vision_framework config/real_robot.yaml
```

使用脚本：

```bash
sudo ./scripts/run_real_robot.sh
```

日志会保存到 `logs/`。

## 相机预览

直接运行：

```bash
./build/camera_preview_demo config/camera_preview_demo.yaml
```

使用脚本：

```bash
./scripts/run_camera_preview.sh
```

## 串口调试 demo

直接运行：

```bash
sudo ./build/serial_debug_demo --help
sudo ./build/serial_debug_demo --mock
```

使用脚本：

```bash
sudo ./scripts/run_serial_debug.sh --help
sudo ./scripts/run_serial_debug.sh --mock
```

日志会保存到 `logs/`。

## 协议 demo

直接运行：

```bash
sudo ./build/serial_protocol_demo --help
sudo ./build/serial_protocol_demo --mock
sudo ./build/serial_protocol_demo --port /dev/ttyACM0 --baudrate 115200 --listen
```

使用脚本：

```bash
sudo ./scripts/run_serial_protocol_demo.sh --help
sudo ./scripts/run_serial_protocol_demo.sh --mock
sudo ./scripts/run_serial_protocol_demo.sh --port /dev/ttyACM0 --baudrate 115200 --listen
```

日志会保存到 `logs/`。

## 日志保存说明

- 主程序脚本和串口 demo 脚本都会自动创建 `logs/`
- 日志文件名包含时间戳
- `tee` 会同时输出到终端和日志文件
- `logs/` 不应提交到 git

典型日志文件：

- `logs/debug_image_real_serial_YYYYMMDD_HHMMSS.log`
- `logs/real_robot_YYYYMMDD_HHMMSS.log`
- `logs/serial_debug_YYYYMMDD_HHMMSS.log`
- `logs/serial_protocol_demo_YYYYMMDD_HHMMSS.log`

## 常见问题

### 串口权限

如果没有串口权限，常见做法是直接使用 `sudo`：

```bash
sudo ./scripts/run_real_robot.sh
```

### `/dev/ttyACM0` 变成 `/dev/ttyACM1`

这是 USB CDC 重新插拔后的常见现象。

如果配置使用固定端口，需要检查当前设备号：

```bash
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
```

如果配置已支持：

```yaml
serial:
  port: auto
```

则程序会在启动时自动扫描常见端口。

### `port: auto`

`port: auto` 只解决“启动时自动选择串口”，不提供运行中自动重连。

### `sudo` 的使用

在主程序、`serial_debug_demo`、`serial_protocol_demo` 需要访问真实串口时，通常建议加 `sudo`。

### `logs/` 不应提交

`logs/` 仅用于本机调试记录，应保持在 `.gitignore` 中。
