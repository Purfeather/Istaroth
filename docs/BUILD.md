# 从源码构建

Windows x64，PowerShell 7、Python 3.11+、MSVC Tools **14.44.35207**、Windows SDK **10.0.26100.0**。版本记录在根目录 toolchain.json；依赖源码已随目录提供，无需游戏安装、旧包或开发者个人路径。

```powershell
pwsh -NoProfile -File ./build.ps1
# 非标准 VS 安装可明确传入：
./build.ps1 -VcTools '<MSVC Tools/14.44.35207>' -SdkRoot '<Windows Kits/10>'
```

脚本会自动定位标准 Visual Studio 安装路径；CI 流程同样使用此入口。远端 Runner 需安装锁定的工具链版本以保证构建的一致性与可复现性。

`out/` 目录包含五个目标组件、自有模拟宿主／测试程序、`build.json` 和 `test-results.json`。内置测试仅针对模块接口与模拟环境，不直接操作真实游戏进程。发布包需通过专用打包脚本生成。

源码保留原始字节排布，配置 `.gitattributes` 防止换行符自动转换影响校验。如需在本地进行离屏图标校验，可在系统中文字体具备的前提下运行 `python tests/check_icons.py ./out ./out-slim`。

构建脚本统一使用相对路径，并通过宏定义将 ImGui 断言文件名替换为组件标识，避免二进制中残留本地绝对路径。历史已验收归档包保留原始字节用于校验比对，新构建产物则统一采用脱敏后的相对路径编译。

源码目录结构：
- `src/loader`：注入与引导加载器
- `src/resident`：游戏内菜单与驻留控制
- `src/features`：三个功能 DLL（时钟、对话探针、动作执行）
- `src/shared`：共享 ABI 与内部协议
- `profiles/`：当前适配版本的指纹与兼容性数据

当前源码构建统一使用 MSVC（C11 / C++17）编译全部组件，使用官方固定提交的 Detours 源码生成静态库。

## 游戏更新维护

游戏版本更新后，不匹配的指纹将自动阻止注入加载。适配维护流程：
1. 运行 `python tools/check_game.py '<YuanShen.exe>'` 获取新版文件指纹与特征。
2. 复核并更新 `profiles/` 中的指令特征与结构体偏移。
3. 运行本地模拟测试套件确认 ABI 与功能逻辑无误。
4. 完成实机测试验证后，更新支持配置与发布记录。

## 七文件精简包打包

运行命令生成精简发布目录及对应压缩包：
```powershell
./build.ps1 -Slim -OutputDirectory ./out-slim
python tools/package.py --slim ./out-slim
```

0.2.0 主包包含 EXE、四个 DLL、LICENSES.txt 和使用说明.txt。说明源文件为 docs/USAGE.txt，打包时转换为带 BOM 的 UTF-8 和 Windows 换行；配置、日志及独立报告工具不进入主包。

## 独立报告工具

在 x64 开发者 PowerShell 中运行：

```powershell
./tools/build_report_tool.ps1
# 也可按上方工具链版本明确传入 -VcTools 与 -SdkRoot。
```

输出为 `out/report-tool/伊斯塔露报告工具.exe`，静态链接运行库并内嵌项目 LICENSE 和 NOTICE；无需随附 DLL。脚本只构建报告工具，并运行图形窗口导出测试及原日志导出回归，不构建或启动游戏组件。测试程序和构建日志留在输出目录，不随 EXE 分发。报告工具独立版本为 0.1.0，与游戏组件版本分别记录。

完整 build.ps1 也会在其输出目录的 report-tool 子目录构建该工具，默认运行对应离线检查及七文件打包回归。打包脚本仍只取七个主包文件。
