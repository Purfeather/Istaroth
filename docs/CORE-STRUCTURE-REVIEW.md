# 核心 DLL 结构与验收

视觉和行为基线为 `c69b3ca`。审查范围是 `src/resident` 中的项目源码。两个构建配置复用相同的 UI、状态和材质实现。

| 文件 | 处理结果 | 验证依据 |
| --- | --- | --- |
| resident_ui.cpp | 标题、总开关、倍率、功能行和页脚各自绘制，窗口统一控制布局、裁剪、透明度。HUD 统一计算标签宽度。 | 菜单操作用例、状态及过渡快照 |
| resident_ui.h | 集中声明菜单状态提示和视口缩放函数。既有状态类型和命令签名保持兼容。 | 离线窗口、快照与正式目标分别编译 |
| resident.cpp | 设置读取从工作循环移出，配置读写和命令使用相同的功能位名称。实时 UI 与快照共用面板提示。保留观测、判定、发布、持久化的执行顺序。 | 双配置构建、状态回归、快照对照 |
| resident_control.h | 展开状态和原因的分支，明确故障、初始化、恢复与等待的显示优先级。 | 正常、缺失、过期、争用和错误优先级用例 |
| resident_renderer.cpp | 独立字体选择，初始化和逐帧绘制复用缩放公式，按资源归属展开清理步骤。 | 自有 WARP 宿主的窗口尺寸变化、设备重建和管线状态恢复 |
| resident_material.h | 区分输入纹理准备与整套材质清理，列明各类资源的释放。 | 模糊强度切换、清理后重建、局部区域、多重采样及色值测试 |
| resident_cursor.h | 保留现有实现。保存、显示和恢复已经分开，恢复时检查是否仍有权修改全局光标。 | 前台、失焦、失败及显示计数测试 |
| resident_icon.h | 保留现有实现。文件验证和图像解码已有明确边界，设备纹理由同一条路径创建。 | 外部 PNG 和有效覆盖图的对照 |
| resident_embedded_icon.h | 保留现有实现。精简版先尝试外部文件，回退路径复用相同的帧转换。 | 缺失文件、无效文件和目录的回退对照 |
| resident_internal.h | 保留现有声明。内部模块边界清晰，本轮无需扩大共享状态。 | 标准版、精简版链接和协议大小检查 |
| scene_blur.hlsl | 保留已验收的采样及编码公式。CPU 常量布局与着色器继续一一对应。 | WARP 材质图像、输入边界及首次绘制基准 |

## 复验步骤

使用项目版本锁定的 x64 MSVC、Windows SDK、微软雅黑和 PowerShell 7。以下路径变量均指本机的绝对路径。先用基线工作树的 `build.ps1` 构建基线，再用当前工作树运行以下命令。输出目录放在源代码目录之外。

```powershell
& "$BaselineSource/build.ps1" -Slim -OutputDirectory $BaselineSlim
& "$BaselineSource/build.ps1" -OutputDirectory $BaselineStandard
./build.ps1 -Slim -OutputDirectory $CandidateSlim
./build.ps1 -OutputDirectory $CandidateStandard
python -B tests/check_icons.py $CandidateStandard $CandidateSlim
./tests/compare_resident.ps1 -BaselineSource $BaselineSource -Baseline $BaselineSlim -Candidate $CandidateSlim -OutputDirectory $Comparison
```

对照脚本会把同一套交互与性能驱动链接到基线对象，核对同机解码像素并记录七组交替采样。耗时指标为首次材质绘制、HUD 和菜单的 CPU 提交或帧构建时间。一次采样超过基线加 `max(0.2 ms, 5%)` 时整组复测，原始结果保存在输出目录。人工验收在独立窗口中操作总开关、四档倍率和三个功能开关，再测试关闭和重新打开菜单。
