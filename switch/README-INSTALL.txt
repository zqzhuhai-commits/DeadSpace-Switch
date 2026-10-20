Dead Space Switch 0.1.0 — 仅开发诊断版

本包不是可玩版，不会进入《死亡空间》游戏。
ARM32 游戏执行后端和游戏接口尚未完成；尚未实机测试。

1. 将 sdcard 目录里的 switch 文件夹复制到 SD 卡根目录。
2. 通过 Homebrew Menu 的应用模式/title takeover 运行。
3. A 扫描游戏数据；X 测图形/PVRTC/音频；Y 测 GLES1；+ 退出。
4. 日志位置 /switch/deadspace/diagnostics.log。

无需游戏文件，也可以运行平台诊断。
原版数据可用 source/switch/tools/prepare_data.py 导入；不会导入VPK启动器。
详细编译、数据布局和开发状态见 source/README.md。
