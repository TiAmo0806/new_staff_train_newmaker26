# bean_digit_v1 确认版模型导出

导出日期：2026-06-24

## 来源

本目录模型来自最终训练结果：

```text
D:/yolo/runs/train/bean_digit_v1/weights/best.pt
```

这是 `bean_digit_v1` 训练完成后的最佳权重，不是其他队友模型。

## 导出命令

```powershell
D:\miniconda3\envs\pytorch\python.exe -B export_onnx.py --model runs/train/bean_digit_v1/weights/best.pt --imgsz 640 --opset 18
```

## 文件

```text
best.pt
best.onnx
best.onnx.data
classes.txt
```

`best.onnx` 和 `best.onnx.data` 必须放在同一个目录。C++ 程序只传入 `best.onnx` 路径，但不能缺少 `best.onnx.data`。

## ONNX 信息

```text
input shape: 1 x 3 x 640 x 640
output shape: 1 x 12 x 8400
opset: 18
```

## 类别顺序

```text
0 soybean
1 mung_bean
2 white_kidney_bean
3 data_1
4 data_2
5 data_3
6 data_4
7 data_5
```

## SHA256

```text
best.pt        2A00A833351B4A4801CBBDD8E945D2294BA9A5068E2F811A6DFFCD0D9B595D77
best.onnx      C4D744394EBE03D52E1C3EDF51664FC45FAC2B23D40E928AFF6A6784E3D74096
best.onnx.data C5C79B90D1237193D32979E35C694CDDA776B95A403B541D78946B155A0CC7E9
```

## 虚拟机使用建议

请把整个目录复制到虚拟机，不要只复制 `best.onnx`。

推荐运行时确认：

```text
best.onnx
best.onnx.data
classes.txt
```

三者在同一个模型目录里。
