# 开发分支

`develop` 用于保存正在开发、尚未完成全部测试的修改。`main` 保留正式发布的源码；已验收安装包从 [Release](https://github.com/Purfeather/Istaroth/releases) 获取。

## 日常开发

在本仓库根目录执行：

```powershell
git switch develop
git pull --ff-only
# 修改源码后，检查改动并提交具体文件。
git status
git add <文件路径>
git commit -m "fix(module): describe the change"
git push origin develop
```

较大的实验可以从 `develop` 创建单独的功能分支，完成后通过 PR 汇入 `develop`。

## 构建与验证

工具链版本见 [构建说明](BUILD.md) 和 [toolchain.json](../toolchain.json)。构建与离线测试不需要安装游戏：

```powershell
python -B tools/check_public_tree.py
pwsh -NoProfile -File ./build.ps1 -Slim -OutputDirectory ./out-slim
python tools/package.py --slim ./out-slim
```

`main` 推送和 PR 自动运行 Windows CI。`develop` 推送不自动构建；先在本地检查，准备验证开发包时手动触发：

```powershell
gh workflow run build.yml --ref develop
```

CI 通过表示自动构建和离线检查通过，实机测试结果需单独记录。

## 开发包与正式版本

- 开发包应标注源码提交短哈希，以及离线测试、实机测试的通过和未验证项；保留对应构建清单与 SHA-256。
- Git 分支保存源码，EXE、DLL 和 ZIP 不直接提交到 Git。开发包经批准分发时，使用标记为 **Pre-release** 的 GitHub Release 或 CI 附件。
- 发布正式版本前完成相应验收，再通过 PR 将改动合入 `main` 并创建版本标签。日常推送 `develop` 不自动创建 Release。
- v0.1.1 的已验收安装包和标签保持不变，作为现有正式版本及回退基线。
