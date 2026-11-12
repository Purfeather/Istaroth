# 源码与依赖来源审计

审计日期：2026-11-13。公开范围仅当前原生实现，旧原型／调试工具及其 BetterGI GPL 图片不在发布目录，也不在当前原生包。不能据此将它们重新授权。

| 内容 | 处理和依据 |
|---|---|
| 加载器、驻留、UI、鼠标、控制器与自有测试 | 从本工作区当前原生源码逐项复制，来源哈希记录在 SOURCE-ORIGINS.json；原创贡献按 Apache-2.0，保留既有第三方归属。 |
| time_core / story_probe | 本工作区的时钟与只读识别实现；公开最小版本数据，不携带完整游戏 SDK／转储。兼容性头内的短指令检查数据源自游戏，仅为特定构建的核验数据，不声称拥有游戏代码。 |
| dialogue_actions / dialogue_cooldown | 参考 Akebi DialogSkip 的对话事件流程与胡桃 FastSkipTalk 的冷却条件思路；此处使用独立门禁、动作线程验证、限定对象和可恢复钩子，没有复制上游框架或原始无条件 NOP 实现。保守保留参考来源和许可证，不宣称全部技术从零发明。 |
| Dear ImGui | 官方 v1.91.9b，提交 f5befd2d29e66809cd1110a152e375a7f1981f06，MIT。固定文件哈希见 third_party/imgui/SOURCE.json。 |
| Microsoft Detours | 官方 v4.0.1，提交 e4bfd6b03e50de46b47abfbd1e46b384f0c5f833，MIT。官方归档和选定源码哈希见 third_party/detours/SOURCE.json。干净构建从这些源文件生成静态库。 |
| 已验收包的 Detours | 历史 vendor 静态库来源为本地 Akebi 3.6 附带库；保留 Microsoft MIT 声明。其头标称 4.0.1，但不声称与官方固定提交的编译字节／实现完全一致。已验收成品与新构建分别标识。 |
| Akebi 3.6 | 本地参考根许可证为 Apache-2.0；不存在随本次筛选复制的上游框架／SDK。相关参考文件的本地哈希见 SOURCE-ORIGINS.json。未找到该快照根 NOTICE，不虚构上游署名或提交号。 |
| 胡桃重制版与 UnlockerIsland | 主项目 MIT 原版权保留；实际 FastSkipTalk.cpp 所在 UnlockerIsland 的 MIT 文件含 [year] [fullname] 占位符，原样保留，不自行填入权利人。本次对照未发现其函数体被复制；这里只采用条件跳过思路，故不把占位符当成已核实的版权人。若后续导入其表达性代码，必须先明确权利链。 |
| Ex_M | 仅运行行为参考；没有导入其可执行程序、资源或实现源码。 |
| 图标与字体 | 见 assets/RIGHTS.md；不捆绑 Windows 字体、角色参考原图、游戏截图。 |

比对范围：当前发布的项目源码，与 Akebi DialogSkip.cpp、GameSpeed.cpp 和胡桃 FastSkipTalk.cpp 的本地快照进行结构检查及连续非空行对比。未发现相同的长函数体；这不是对所有可能来源的穷尽性版权鉴定。对独立实现与思路参考作上述区分，不将“有根许可证”当作所有素材均有授权。

发布前必须解决 assets/RIGHTS.md 的 Bot 形象权利待确认项。公开的最小兼容性数据也不授予使用游戏或修改游戏的许可。
