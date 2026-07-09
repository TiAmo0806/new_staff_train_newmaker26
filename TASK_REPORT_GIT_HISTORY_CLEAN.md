# Git History Clean Report

## 执行步骤

执行的 Git 操作：

- `git status`
- 确认 `lzy_git_cleanup` 已包含提交 `chore: clean git tracked files`
- `git branch clean_backup`
- `git checkout --orphan lzy_clean`
- `git add .`
- 发现 `onnxruntime` 被子目录 `.gitignore` 反忽略，停止提交并排查
- 修正 `bean_vision_framework/.gitignore` 中的 ONNX Runtime 规则
- `git reset`
- `git add .`
- `git status`
- 验证 `linuxSDK`、`onnxruntime`、`build`、`*.onnx`、`*.onnx.data`、`cache` 未进入 staged `new file`
- 配置当前仓库本地提交身份：
  - `git config user.name "darkttsave"`
  - `git config user.email "lzy18142551917@163.com"`
- `git commit -m "feat: initialize clean vision framework"`
- `git log --oneline --max-count=5`

## 新旧历史区别

旧历史：

- 包含已被 Git 跟踪的大文件和污染内容
- 主要包括 `linuxSDK_V2.1.0.49202602041120/`
- 包括 `bean_vision_framework/bisai/onnxruntime-linux-x64-1.27.0/`
- 包括 `bean_vision_framework/bisai/onnxruntime-linux-x64-1.27 (1).0/`
- 包括 `build/`、`CMakeFiles/`、`.cache/`
- 包括 `*.onnx`、`*.onnx.data`、输出图像和缓存文件

新历史：

- 当前分支 `lzy_clean` 只有一个新的初始化提交
- 保留当前干净工程文件
- 不再纳入 Git 历史的内容包括：
  - `linuxSDK`
  - `onnxruntime`
  - `build`
  - `*.onnx`
  - `*.onnx.data`
  - `cache`

## 文件完整性检查

源码是否修改：

- 未修改源码逻辑，仅进行了 Git 历史整理和 `.gitignore` 调整

CMake是否修改：

- 未修改 CMake 文件内容

配置是否修改：

- 未修改配置文件内容

## 验证结果

`git status` 结果：

```text
On branch lzy_clean
nothing to commit, working tree clean
```

`git log --oneline --max-count=5` 结果：

```text
79ec41d feat: initialize clean vision framework
```

验证结论：

- 当前 `lzy_clean` 只有一个根提交
- 新历史已与旧历史切断
- 工作区干净
- 禁止项未进入新的提交历史

## 后续操作建议

下一步如需推送到远程，建议：

1. 先确认远程分支名称和替换策略
2. 如果要用新历史替换旧分支，先与团队确认是否允许重写远程历史
3. 推荐先推送到新分支进行审核，例如：
   - `git push origin lzy_clean`
4. 如果确认要替换原分支，再由你手动决定是否执行强制推送
5. 在远程替换前，保留本地 `backup_before_cleanup` 和 `clean_backup` 作为保险
