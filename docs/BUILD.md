# 从源码构建

Windows x64，PowerShell 7、Python 3.11+、MSVC Tools **14.44.35207**、Windows SDK **10.0.26100.0**。版本记录在根目录 toolchain.json；依赖源码已随目录提供，无需游戏安装、旧包或开发者个人路径。

```powershell
pwsh -NoProfile -File ./build.ps1
# 非标准 VS 安装可明确传入：
./build.ps1 -VcTools '<MSVC Tools/14.44.35207>' -SdkRoot '<Windows Kits/10>'
```

脚本自动定位标准安装，工具链不匹配明确失败；CI 同样执行该入口。托管 CI 尚未在远端运行，若 runner 未安装指定组件，须安装锁定版本后再运行，不自动改用浮动版本。

out/ 包含五个重建组件、自有宿主／测试、build.json 和 test-results.json。测试只操作自己创建的进程与模拟 Win32 状态，不访问游戏。不把 out/ 整目录直接当发行包。

源码布局：src/loader 为加载器，src/resident 为菜单与驻留控制，src/features 为三个功能 DLL，src/shared 为共享 ABI，profiles/ 为当前版本的最小兼容性数据。

当前公开构建统一使用 MSVC C11/C++17。历史已验收包的两个 C DLL 使用 Zig，Detours 使用历史 vendor 库；这两条构建链不保证产生相同字节。新源码构建不是既有实机验收记录的对象。

## 游戏更新维护

先通过 `python tools/check_game.py '<YuanShen.exe>'` 做本地文件指纹核验。更新后保留拒绝加载，离线复核 profiles 中每处数据、ABI和模块指纹；完成自有宿主回归及另行实机验收后才能更新支持记录。不能仅修改哈希来允许未知版本。

## 精简候选

`./build.ps1 -Slim -OutputDirectory ./out-slim` 构建独立 v0.1.1 候选，不能覆盖已验收 v0.1.0。`python tools/package.py --slim ./out-slim` 打包六文件候选，仍需实机验收。
