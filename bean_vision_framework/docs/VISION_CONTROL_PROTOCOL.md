# 视觉-电控通信设计方案

版本：V1.0

用途：视觉与电控进入串口联调阶段使用

---

# 1. 系统职责划分


## 视觉负责

- 图像采集
- YOLO检测
- ROI解析
- 结果融合
- 任务生成


## 电控负责

- 底盘运动
- 云台pitch控制
- 云台yaw控制
- 电机角度调整


---

# 2. 控制原则


视觉不获取：

- yaw角度
- pitch角度


原因：

- 无姿态解算
- 参数由比赛前调整


采用：

```text
离散动作控制
```

---

# 3. 豆子阶段通信


## 电控 → 视觉

发送：

```text
BEAN_AREA_READY
```


视觉收到后：

```text
开始豆子识别
```


完成：

视觉发送：

```text
BEAN_RESULT
```

---

# 4. 数字阶段通信


## 第一步

电控：

```text
DIGIT_AREA_READY
```


视觉进入数字模式。


---

## 第二步

视觉请求：

```text
REQUEST_DIGIT_POSE
```


电控：

执行pitch旋转。


完成：

```text
DIGIT_POSE_READY
```

---

# 5. Yaw扫描流程


## VIEW0


视觉：

```text
REQUEST_VIEW0
```


电控：

转固定角度。


完成：

```text
VIEW0_READY
```


视觉：

执行识别。


---

## VIEW1


视觉：

```text
REQUEST_VIEW1
```


电控：

转固定角度。


完成：

```text
VIEW1_READY
```


视觉：

执行识别。


---

## VIEW2


视觉：

```text
REQUEST_VIEW2
```


电控：

转固定角度。


完成：

```text
VIEW2_READY
```


视觉：

执行识别。

---

# 6. 推荐通信状态


## 视觉 → 电控


```text
START_BEAN

REQUEST_DIGIT_POSE

REQUEST_VIEW0

REQUEST_VIEW1

REQUEST_VIEW2

TASK_RESULT
```


---

## 电控 → 视觉


```text
BEAN_AREA_READY

DIGIT_AREA_READY

DIGIT_POSE_READY

VIEW0_READY

VIEW1_READY

VIEW2_READY
```

---

# 7. 不推荐设计


不要：

```text
视觉发送 yaw=30°
```


原因：

视觉无法知道真实角度。


不要：

```text
视觉判断云台是否到位
```


原因：

没有姿态反馈。

---

# 8. 联调流程


## 阶段1：虚拟串口

测试：

```text
视觉发送命令

模拟电控回复
```


---

## 阶段2：真实C板


验证：

```text
消息

↓

动作

↓

反馈

↓

识别

↓

结果
```

---

# 9. 后续优化


可以增加：

- 超时机制
- 重试机制
- 状态同步
- 错误恢复


例如：

```text
等待VIEW1_READY超过2s

重新请求
```

---

# 当前目标

完成：

视觉 ↔ 电控

完整闭环。