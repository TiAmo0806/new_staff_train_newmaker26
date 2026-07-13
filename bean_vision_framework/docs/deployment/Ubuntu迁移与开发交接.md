# Ubuntu迁移与开发交接

## 1. 当前工程目标

本工程目标是形成：

```text
四个主程序运行模式
+
两个独立调试 demo
```

且始终保持同一套主流程，不复制多套主程序。

## 2. 当前整理后的文档与结构

当前文档已按功能收敛到：

- 运行模式说明
- 架构与数据流
- 调试与排障
- NUC 部署
- 串口协议
- 本交接文档

根目录只保留入口级说明和 AI/工具所需说明文件。

## 3. 当前代码实现状态

已具备：

- 单一 `main.cpp` 切换四种模式
- ONNXRuntime 作为主推理后端
- `debug_camera_mock` 多帧投票
- `debug_image_real_serial` 真实串口发送
- `real_robot` 串口命令接入
- `serial_protocol_demo`
- `camera_preview_demo`

## 4. 后续开发优先级

建议顺序：

1. 在 Ubuntu/NUC 上完成实际编译
2. 用 `camera_preview_demo` 校准 ROI
3. 用 `debug_camera_mock` 验证真实画面下的多帧稳定识别
4. 用 `serial_protocol_demo` 验证和 C 板的收发
5. 最后实测 `real_robot`

## 5. 改动边界建议

继续开发时应保持：

- 不重写整个工程
- 不复制多套主流程
- 不让 `Detector`/`Protocol` 感知具体运行模式
- 配置差异尽量放在 `InputManager`、命令源、串口和调试层

## 6. 迁移注意事项

- Linux 下模型路径应指向 `bisai/bean_digit_v1_verified_20260624/best.onnx`
- `best.onnx.data` 不能缺失
- 工业相机场景应优先使用 `mindvision_camera`，不要默认退回 OpenCV `camera`
- 若无图形界面，关闭 `show_window`，只保存图片

## 7. 仍需注意的风险

- 当前环境若没有 `cmake/g++/OpenCV`，无法本地完成构建验证
- 串口 ACK 逻辑已接入，但仍需和真实 C 板联调确认包格式细节
- `real_robot` 的稳定性依赖现场相机与 ROI 标定结果
