# Bean Vision Framework

这是一个用于豆子识别、数字箱识别、任务生成和串口协议发送的 Linux C++ 视觉框架。

核心流程保持为一套主链路：

```text
Input / Command
-> BeanNumberDetector
-> RoiParser
-> TaskStateMachine
-> VisionMemory
-> TaskGenerator
-> Protocol
-> SerialPort
```

## 快速构建

本工程当前只考虑 Linux 运行环境，工业相机默认使用 MindVision Linux SDK。

如果 SDK 在这些常见位置之一，CMake 会自动搜索：

- `./camera/linuxSDK_V2.x.x`
- `../linuxSDK_V2.x.x`
- `../camera/linuxSDK_V2.x.x`

如果 SDK 不在默认路径，构建时显式传入：

```bash
cmake -S . -B build -DMINDVISION_ROOT=/path/to/linuxSDK_V2.x.x
cmake --build build -j$(nproc)
```

如果 SDK 已放到项目根目录常见位置，也可以直接：

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
```

## 快速运行

主程序：

```bash
cd build
./bean_vision_framework ../config/debug_command_image.yaml
./bean_vision_framework --help
```

串口协议 demo：

```bash
./serial_protocol_demo --mock
./serial_protocol_demo --help
```

工业相机预览 demo：

```bash
./camera_preview_demo ../config/camera_preview_demo.yaml
./camera_preview_demo --help
```

说明：

- `./bean_vision_framework --help` 会打印主程序用法、支持的配置模式和命令驱动模式下的终端命令
- `./serial_protocol_demo --help` 会打印串口 demo 的参数说明、设备名说明和默认行为
- `./camera_preview_demo --help` 会打印预览 demo 的配置文件用法和按键说明

## 文档索引

- [文档导航](./docs/文档导航.md)
- [运行模式与主程序说明](./docs/运行模式与主程序说明.md)
- [系统架构与数据流](./docs/系统架构与数据流.md)
- [调试与排障指南](./docs/调试与排障指南.md)
- [Ubuntu迁移与开发交接](./docs/Ubuntu迁移与开发交接.md)
- [NUC部署指南](./docs/NUC部署指南.md)
- [串口协议说明](./docs/串口协议说明.md)
- [串口协议_核心协议](./docs/串口协议_核心协议.md)
- [串口协议_电控端](./docs/串口协议_电控端.md)
- [串口协议_算法端](./docs/串口协议_算法端.md)
- [文件映射表](./docs/文件映射表.md)

## 当前运行模式

- `debug_command_image`
- `debug_camera_mock`
- `debug_image_real_serial`
- `real_robot`

## 当前独立 demo

- `serial_protocol_demo`
- `camera_preview_demo`
