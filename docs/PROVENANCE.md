# 源码与依赖来源审计

审计日期：2026-11-13。发布范围仅包含当前原生 C/C++ 实现及配套资源；历史原型、调试工具及外部 GPL 资源不属于本项目开源发布内容。

| 内容 | 处理和依据 |
|---|---|
| 加载器、驻留、UI、鼠标、控制器与自有测试 | 从本工作区当前原生源码逐项复制，来源哈希记录在 SOURCE-ORIGINS.json；原创贡献按 Apache-2.0，保留既有第三方归属。 |
| time_core / story_probe | 时钟控制与只读剧情状态探测实现，仅包含当前版本必须的指令特征数据，不携带游戏完整 SDK 或内存转储。 |
| dialogue_actions / dialogue_cooldown | 参考 Akebi DialogSkip 的对话时序逻辑与 Snap Hutao FastSkipTalk 的冷却跳过思路。采用独立的前台门禁、线程校验与可恢复钩子实现，未引入上游框架代码。保留相关项目参考来源与许可证声明。 |
| Dear ImGui | 官方 v1.91.9b，提交 f5befd2d29e66809cd1110a152e375a7f1981f06，MIT。固定文件哈希见 third_party/imgui/SOURCE.json。 |
| Microsoft Detours | 官方 v4.0.1，提交 e4bfd6b03e50de46b47abfbd1e46b384f0c5f833，MIT。官方归档和选定源码哈希见 third_party/detours/SOURCE.json。干净构建从这些源文件生成静态库。 |
| 历史 v0.1.0 包的 Detours | 历史测试包采用的 Detours 静态库源自本地依赖，头文件标称 4.0.1，保留 Microsoft MIT 声明；v0.1.1 已改用官方固定提交源码构建。 |
| Akebi 3.6 | 本地参考项目的许可证为 Apache-2.0；未引入上游框架源码或 SDK。相关参考文件哈希记录于 SOURCE-ORIGINS.json。 |
| 胡桃重制版与 UnlockerIsland | 保留上游项目 MIT 许可证原文及占位符原样。实现上参考了条件跳过思路，未直接复制代码。 |
| Ex_M | 仅作为黑盒运行行为参考；未引入其可执行文件、二进制资源或源码。 |
| 图标与字体 | 详见 assets/RIGHTS.md；发布包不附带系统字体、角色原画或游戏截图。 |

**源码比对说明**：本次对照范围为项目原生实现与 Akebi 的 DialogSkip.cpp、GameSpeed.cpp，以及胡桃的 FastSkipTalk.cpp。结构检查和连续有效行比对未发现对应函数体的直接复制；具体来源与哈希见 SOURCE-ORIGINS.json。

**素材与兼容数据**：Bot 图标的使用范围与二创属性见 assets/RIGHTS.md；`profiles/` 目录中的特征数据仅用于运行时版本兼容性校验。
