# Git Branch Setup Report

## 当前状态

- 当前分支：`feature_real_robot_pipeline`
- 创建前确认基线分支：`lzy`
- 当前 HEAD commit：`4958777a851a517983dfa075d4f06f85bfe465e3`
- 本地分支：
  - `backup_before_cleanup`
  - `clean_backup`
  - `feature_real_robot_pipeline`
  - `lzy`
  - `lzy_git_cleanup`
  - `main`
- 远程分支：
  - `origin/lzy`
  - `personal/lzy`
  - `personal/lzy_clean`

## 创建分支

- 已确认当前 HEAD 原先位于 `lzy`
- 已从 `lzy` 创建新分支：
  - `feature_real_robot_pipeline`
- 分支指向关系：
  - `lzy` -> `4958777a851a517983dfa075d4f06f85bfe465e3`
  - `feature_real_robot_pipeline` -> `4958777a851a517983dfa075d4f06f85bfe465e3`
- 当前未执行 `push`

## 保留分支

- `lzy`
  - 作用：稳定基线分支
  - commit：`4958777a851a517983dfa075d4f06f85bfe465e3`
  - 说明：应继续保留，不直接承载比赛功能开发
- `feature_real_robot_pipeline`
  - 作用：后续比赛主流程开发分支
  - commit：`4958777a851a517983dfa075d4f06f85bfe465e3`
  - 说明：已创建，等待后续开发使用
- `backup_before_cleanup`
  - commit：`c624bb60445edf6de447d0d906d89229b581fa54`
  - 说明：存在独立价值，代表清理阶段更早的历史节点
- `clean_backup`
  - commit：`d859d8cda3c39bff9794d9bc5d5279ccb72b9bd8`
  - 说明：当前先保留，代表 Git 清理阶段备份点
- `lzy_git_cleanup`
  - commit：`d859d8cda3c39bff9794d9bc5d5279ccb72b9bd8`
  - 说明：当前先保留，但与 `clean_backup` 指向同一 commit

## 待删除分支

- `clean_backup`
  - 当前存在
  - 指向 commit：`d859d8cda3c39bff9794d9bc5d5279ccb72b9bd8`
  - 判断：与 `lzy_git_cleanup` 完全重复，独立价值较低
- `lzy_git_cleanup`
  - 当前存在
  - 指向 commit：`d859d8cda3c39bff9794d9bc5d5279ccb72b9bd8`
  - 判断：与 `clean_backup` 完全重复，独立价值较低

## 风险说明

- 当前未执行远程 `push`，远端尚不存在 `feature_real_robot_pipeline`
- `main` 当前仍指向较早 commit：`c624bb60445edf6de447d0d906d89229b581fa54`
- `backup_before_cleanup` 与 `main` 当前指向同一 commit，删除前需确认是否还需要保留“清理前备份”语义
- `clean_backup` 与 `lzy_git_cleanup` 指向同一 commit，若后续删除，建议二选一保留以减少混乱

## 后续开发建议

- 后续比赛功能只在 `feature_real_robot_pipeline` 上开发
- `lzy` 仅作为稳定回退基线，不直接承载功能迭代
- 删除旧备份分支前，先确认是否需要保留：
  - 一个“清理前”备份分支
  - 一个“清理后”备份分支
- 等你确认后，再单独执行旧分支清理
