# Branch Cleanup Report

## 当前状态

- 当前分支：`feature_real_robot_pipeline`
- 删除操作仅作用于本地分支
- 未删除任何远程分支
- 未执行 `push`

## 删除前确认

- `clean_backup`
  - commit：`d859d8cda3c39bff9794d9bc5d5279ccb72b9bd8`
  - subject：`chore: clean git tracked files`
- `lzy_git_cleanup`
  - commit：`d859d8cda3c39bff9794d9bc5d5279ccb72b9bd8`
  - subject：`chore: clean git tracked files`

## 已删除本地分支

- `clean_backup`
- `lzy_git_cleanup`

## 当前保留分支

- `main`
- `lzy`
- `feature_real_robot_pipeline`
- `backup_before_cleanup`

## 删除后本地分支

- `backup_before_cleanup`
- `feature_real_robot_pipeline`
- `lzy`
- `main`

## 远程分支

- `origin/lzy`
- `personal/lzy`
- `personal/lzy_clean`

## 风险说明

- 本次仅清理本地重复备份分支，未影响当前开发分支和稳定基线
- `backup_before_cleanup` 仍保留为更早历史节点回退点
- 远程侧未同步本次清理结果；如果后续需要统一远程分支结构，应单独确认后再处理
