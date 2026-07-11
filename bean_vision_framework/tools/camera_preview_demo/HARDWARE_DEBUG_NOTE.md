# 工业相机部署注意事项

## MindVision USB连接要求

实验发现：

USB2.0拓展坞无法稳定支持MindVision工业相机。

表现：

- lsusb可以识别设备
- SDK无法初始化
- 出现：
  user control fd open failed


解决：

使用USB3.0直连接口。

原因：

工业相机需要稳定高速USB控制和数据传输。