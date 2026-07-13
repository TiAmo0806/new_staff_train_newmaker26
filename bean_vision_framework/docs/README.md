# 项目文档入口

本文档回答两个问题：

- 现在应该先看什么
- 各类文档分别放在哪里

## 新人 / 上车调试优先阅读

- [BUILD_AND_RUN.md](./BUILD_AND_RUN.md)
  - 当前有效的编译、运行、可执行程序入口
- [ARCHITECTURE.md](./ARCHITECTURE.md)
  - 当前代码事实对应的整体架构
- [CONFIG_REFERENCE.md](./CONFIG_REFERENCE.md)
  - 当前配置字段和解析规则
- [runtime/RUN_COMMANDS.md](./runtime/RUN_COMMANDS.md)
  - 当前有效的常用启动命令
- [protocol/串口协议说明.md](./protocol/%E4%B8%B2%E5%8F%A3%E5%8D%8F%E8%AE%AE%E8%AF%B4%E6%98%8E.md)
  - 当前有效的串口协议入口
- [debug/SERIAL_TEST_CHECKLIST.md](./debug/SERIAL_TEST_CHECKLIST.md)
  - 当前有效的上车串口测试清单

## 分类目录

- [architecture/](./architecture/)
  - 架构与数据流
- [runtime/](./runtime/)
  - 运行模式与启动命令
- [deployment/](./deployment/)
  - NUC、Ubuntu、部署说明
- [camera/](./camera/)
  - 相机系统
- [protocol/](./protocol/)
  - 串口协议
- [debug/](./debug/)
  - 调试、排障、上车测试
- [design/](./design/)
  - 设计提案与方案
- [reference/](./reference/)
  - 文件索引与映射
- [history/](./history/)
  - 阶段总结和历史归档

## 说明

- 当前有效文档优先看根目录，以及 `runtime/`、`protocol/`、`debug/`
- `design/` 下多为设计方案，不一定完全等于当前实现
- `history/` 下为阶段记录，不应直接当作当前代码事实
- 完整索引请看 [文档导航.md](./文档导航.md)
