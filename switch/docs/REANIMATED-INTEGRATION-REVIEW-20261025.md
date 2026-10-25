# Reanimated → Switch：下一版本集成审查

审查日期：2026-10-25。结论：**纹理方案在技术上有条件可行，但没有确认任何 Reanimated 代码或资源同时满足兼容及授权条件；本次不导入、不启用、不分发。** 本提交只增加审查记录，不改运行时、构建默认值、donor 白名单或资源。

## 固定审查对象

| 对象 | 固定版本 | 检查范围 |
| --- | --- | --- |
| [GINESTR0/DeadSpaceReanimated](https://github.com/GINESTR0/DeadSpaceReanimated/tree/89fd28abdcb8ba53162e6152b354d4a943d89567) | `89fd28abdcb8ba53162e6152b354d4a943d89567` | Git 跟踪树、README、发布资产元数据 |
| DeadSpace-Switch main | `48fcea2663a7b87e5ad387015ab910953b6275c2` | README、NOTICE/LICENSE、两个 Makefile、提取/RomFS 工具、纹理运行时和测试、既有审计报告 |
| [0.4 审查 PR #6](https://github.com/zqzhuhai-commits/DeadSpace-Switch/pull/6) | `ab2047eeb69b16f53380f2f1f6c804e87e1271aa` | README、Switch Makefile、rebuild_main.cpp、game_graphics_probe.cpp；与 main 分开评价 |

Reanimated 跟踪树只有 README 和五张 PNG 截图；没有核心源码、构建配置、LICENSE 或独立高清包。README 的原生分辨率、2x/4x 高清、60 fps、声音与控制修复是作者陈述，不能据此验证 Switch 上的同等行为。

[v1.3 发布页](https://github.com/GINESTR0/DeadSpaceReanimated/releases/tag/v1.3) 只有一个 APK 资产：715,366,829 字节，GitHub 元数据 SHA256 为 `dbd94ee54fc3fefdddb101fac4eba1e2058a5db7cfcf30ac41ad96e88b285a3a`。发布说明正文误写 1.2 文件名，实际资产为 `DeadSpaceReanimated-1.3.apk`。自动生成的源码压缩包不是游戏源码。

## 分项决策

| 候选部分 | 技术判断 | 授权与本次决定 |
| --- | --- | --- |
| Android ARM64 自定义运行时 `libdsr.so` | Android ABI、系统服务与 Switch/libnx 不同；均为 ARM64 不代表可直接加载。未提供源码，不能准备源码移植 | 未找到复用许可证，不复制或导入 |
| APK 内 ARM32 游戏库 `libDeadSpace32.so` | 既有审计 SHA1 `64a3323cd3cc0c483a645b62af8508624163ba9b`，不等于当前受支持的 `0ed42b611415015807f759ec9b5457857143ce39`；已有重定位、补丁和 JNI 假设不能直接套用 | EA 游戏内容；不替换原库，不放宽白名单 |
| `assets/texture_hd/*.ktx` | 既有报告记录 222 个 KTX1、340,222,792 字节；116 ETC1、106 ATC 插值透明度；需要按实际 donor 的原图字节键匹配 | 格式可处理不代表授权允许；本次不提取或内置 |
| APK 原始模型、关卡、声音、字体/界面 | 未证明与支持 donor 一致；不能把整个 assets 当高清覆盖包 | 没有确认再分发许可，不整体替换 |
| 高清算法、声音/输入修复描述 | 可作为需求线索；没有可审查实现，不能声称已移植 | 后续可自行实现，并独立验证，不复制未授权实现 |
| Switch 现有提取、解码和原图回退代码 | 已存在，无需重复开发；项目按 GPL-3.0 发布并保留第三方许可 | 可继续审查和维护本项目代码；项目 GPL 不授予 APK/EA 资源权利 |

## 已有接线与版本保护

根 Makefile 是 Linux ARM32 加载器构建；`switch/Makefile` 是 devkitA64/libnx、SDL2/Mesa 的 AArch64 构建。`CPU_PROBE=1` 才链接 ARM32 执行核心，`ROMFS_ROOT` 负责内置资源。Dynarmic 是固定提交的独立子模块，不能由 Reanimated 的未知运行时直接替代。

`reanimated_hd.py` 锁定 v1.3 大小和 SHA256，验证 KTX 边界并支持对实际 donor 的 M3G 做精确键匹配。`prepare_romfs.py --hd-apk` 仅保留匹配 KTX，零匹配拒绝输出；保持原库和原始资源，先暂存后发布。**这些检查验证技术输入，不是授权审查，也不是发布许可。**

`guest_gl.cpp`/`hd_texture.cpp` 已包含完整 mip 上传、格式解码、原图对象保留、动态纹理回退和默认 128 MiB 高清预算。4096×4096 RGBA8 基础层为 64 MiB，完整 mip 约 85.3 MiB，原图与临时解码缓冲另计；仍需实测峰值和卡顿。

main 的诊断入口可选加载高清目录。PR #6 的 `GAME_LAUNCHER=1` 定义 `DS_GAME_LAUNCHER`，`game_graphics_probe.cpp` 用 `#ifndef DS_GAME_LAUNCHER` 排除高清回调。**因此存在高清文件不代表 0.4 正式启动入口正在使用高清。** 保留该隔离，先处理现有启动与实机问题。

PR #6 的描述记录 0.4.3 实机只见中央十字、未记录第 2 帧返回；0.4.4 为待实机验证的修复候选。不能将 main 的旧诊断说明、主机画面或包内容验证当作当前 Switch 可玩保证。本审查不覆盖或修改用户已有可运行文件。

## 许可结论与集成门槛

Reanimated README/发布声明是非商业爱好项目，并将游戏内容归于 EA；没有提供对修改、移植、转换和再分发高清纹理或运行时的明确许可。“免费”“非商业”“已下架”和可下载不等于授予这些权利。[GitHub 许可说明](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/licensing-a-repository) 也明确区分公开查看/fork 与许可证授予的使用权。

下一版本启用前须完成：

1. 取得覆盖所用文件、修改/转码、Switch 使用与预定分发方式的明确许可，并厘清作者自有高清改动与 EA 原始内容的权利范围；作者对其自有改动的许可不能替代其他权利人的授权。资源清单记录授权来源与适用范围。未完成前不把资源纳入公开 PR、CI 产物或发布包。
2. 用用户实际受支持 donor 运行 `--donor-data`，保存精确匹配数、未匹配键和逐文件摘要。APK 自带原图的 222/222 匹配不证明 PSV donor 也全部匹配。
3. 在独立候选分支及独立输出目录启用可关闭的实验路径，维持原图回退和现有库白名单；禁止覆盖已有可运行包、存档或数据目录。
4. 同场景原图/高清 A/B 核验 HUD、字形、透明度、UV、完整 mip、动态更新、纹理尺寸与显存预算；记录加载耗时、帧时间和峰值内存。
5. 在目标 Switch 验证首关操作、射击、声音、切关、存取档及无外置数据启动。损坏/缺失高清资源必须回退。达到门槛后再提交实际集成 PR，并按许可决定可交付内容。

## 本轮验证及未验证项

本轮重新克隆并静态审查了上述固定源码，读取 GitHub v1.3 发布元数据；**未重新下载 715 MB APK、未重跑真实资源解码、未找到实际 donor、未运行原游戏或 Switch**。222 张纹理及既有解码一致性结果来自仓库已有审计文件，不是本轮新测量。

Windows Python 3.13 下 `python switch/tests/reanimated_hd_test.py` 通过（临时目录设在工作区），覆盖 KTX/mip 边界、哈希向量、M3G 参数及失败清理/拒绝覆盖。`hd_romfs_test.py` 在创建符号链接处因 WinError 1314 停止，不能记为整套通过。当前未发现 g++/devkitA64 可用命令，未进行 C++、原生 GL 或 Switch 构建验证。本提交仅文档，无新增运行代码或资源。

