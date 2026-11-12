# 对外发布检查

- [x] 当前版本 v0.1.1 已由用户实测接受，原 ZIP 不覆盖。
- [x] 只公开原生源码与必要素材／依赖／构建／自有测试；旧原型与调试记录不入库。
- [x] 原创 Apache-2.0；MIT／参考来源及素材例外单列，许可证原文保留。
- [x] 五组件可从固定依赖源码构建；编译和离线检查无需游戏。
- [x] 打包采用白名单，默认配置无个人路径，附文件哈希与 ZIP 校验。
- [x] 六文件包的首次启动、路径与配置保存、菜单和鼠标恢复已获用户实机确认，并保存验收记录。
- [x] v0.1.1 新构建清理历史工作区路径；旧版本归档保持原样。
- [x] 用户已补充 Bot 图标的非商业游戏二创使用说明，相关角色和第三方素材权利单独说明。
- [x] 项目说明与发布文档完成本地润色，去除多余套话与过度防御措辞。
- [x] [Purfeather/Istaroth](https://github.com/Purfeather/Istaroth) 私有空仓库已创建，不初始化 README、许可证或分支。

## 发布执行

main 推送后运行远端 CI，检查通过才发布 v0.1.1。Release 使用原验收包及其 SHA-256 文件，不替换程序内容，仓库保持私有。

执行结果见 [GitHub Actions](https://github.com/Purfeather/Istaroth/actions) 和 [Releases](https://github.com/Purfeather/Istaroth/releases)。
