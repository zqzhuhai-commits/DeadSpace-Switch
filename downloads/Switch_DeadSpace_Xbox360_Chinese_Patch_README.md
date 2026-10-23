# 已知启动故障：请暂停使用旧版完整补丁

用户已反馈旧版 `Switch_DeadSpace_Xbox360_Chinese_Patch.zip` 更新 VIV 后黑屏，无法启动。此前仅完成合成 VIV 上的写入回读检查，没有完成游戏启动验证。请恢复备份的原版 bigfile0.viv；目前尚未确定根因，也没有经过实机验证的修复版。

提供 [VIV 只读诊断工具](Switch_DeadSpace_VIV_Diagnostic.zip)。这不是修复补丁。解压运行 VIV_Diagnostic.exe，分别选择原版和黑屏版 VIV，报告另存为 original_diagnostic.txt 和 patched_diagnostic.txt；发回两个报告，并注明主机或模拟器及黑屏阶段。无需上传整个 VIV。

已发现的待验证风险：字体流由 524288 字节扩大到 22806528 字节；三个资源被移动到 VIV 尾部，按 2048 字节对齐。尚不能判断其中哪项导致黑屏。

诊断工具以只读模式打开 VIV，源码随包提供。读取逻辑通过 Linux 样本检查，Windows EXE 已交叉编译；Windows 图形界面和游戏启动尚未验证。
