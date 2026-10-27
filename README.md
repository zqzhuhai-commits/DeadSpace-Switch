# Dead Space Mobile — Switch 重制测试候选版

**当前 0.4.15-HD 闪退修复候选：修复 0.4.14 在第 121 帧记录统计时调用已移走回调引起的 bad_function_call，保留性能优化和材质。** 见 [闪退原因及验证边界](switch/docs/CRASH-0.4.15-HD.md)。

**0.4.14-HD 性能候选版：基于 0.4.13-HD 保留 mip 兼容处理，合入图形函数缓存、临时内存复用，并缓存可失效的纹理过滤状态。默认仍为 1280×720；本地候选包含 53 张高清材质，实机帧率提升待验证。** 见 [性能改动与验证边界](switch/docs/PERFORMANCE-0.4.14-HD.md)。

**2026-10-25：新增直接启动真实游戏的重制入口；默认不启用高清替换。Windows 主机已验证简体中文、第一关实际画面、角色移动和视角输入；Switch 交叉编译通过，尚未上机验证。独立 NSP 已在用户本地生成并完成解密/内容校验；尚未实机安装或运行，不是全流程认证成品。**

本分支保留旧版作参考，新入口为 `switch/source/rebuild_main.cpp`。重制说明、构建方法和测试边界见 [重制进展](switch/docs/REBUILD-0.4.0.md)。公开下载只包含自制运行时，不包含 EA 游戏库、资源或用户存档；用户提供的资源整合包仅保留在本地。

以下为旧版历史记录，不能代替本次重制验证结果。

---

# 历史 0.3.2 开发记录

**0.3.2 开发基线：已加入 JNI、映像/堆修复及可选原生 GL 桥；当前仍是开发诊断版，不是可玩完整版或 NSP。默认 L 诊断使用模拟 GL，R 使用原生 GLES1 子集。**

[最新协作线程进展](switch/docs/PROGRESS-THREADS-0.3.2.md)：已复核 `1fbe1bb` 调试更新，替换同步线程和强改完成标志的实验路径；ARM32 指令线程交互、上下文及等待/唤醒测试通过。真实 donor 及 Switch 运行尚待验证，未生成 NSP。

[0.3.2 源码审查和 NSP 阻塞项](switch/docs/REVIEW-0.3.2.md)。高清资源准备现支持与实际 PSV 数据匹配，并纳入内置 RomFS 校验；尚未验证完整游戏运行。

[后续 JNI 修复](switch/docs/PROGRESS-JNI-0.3.2.md)：已修正函数表、参数解码、实例接收者、字符串/数组和真实资源读取，通过主机 ASan/UBSan 回归；尚未重跑真实 PSV 库或验证游戏画面。

[后续内存修复](switch/docs/PROGRESS-MEMORY-0.3.2.md)：已删除与栈冲突的第二份映像，修复嵌套调用现场恢复、堆复用及 realloc 越界读取，通过主机回归；协作线程进展见上方最新报告。

[原生 GL 接入](switch/docs/PROGRESS-NATIVE-GL.md)：真实 GL 子集已接到生命周期执行器，并通过主机 Mesa 像素回读；尚未验证 Switch 游戏画面。

[高清运行时接入](switch/docs/PROGRESS-HD-RUNTIME.md)：已加入哈希匹配、KTX 完整 mip 上传、独立高清对象和原图回退，通过主机原生回读；222 张高清图的 2,310 层与独立解码器一致。尚未进行实际 PSV 联调或 Switch 画面验证。

0.2.1 源码已按实机日志修正路径与版本标识：开发目录改为 `deadspacemobile`，增加完整库路径提示、CPU探测开始/结束日志及包内 RomFS 数据源选择。基础图形/PVRTC实机通过；CPU尚无实机结果，4个矩阵调色板入口缺失。详见 [实机日志分析](switch/docs/DEVICE-LOG-20261020.md)。

基于 EapRules/deadspace-native-arm 提交 `cc8fd2c8a16530936aa3c2ce205ef4cffc2814e9`。上游目录保留；原 Linux/PortMaster 说明见 `README.portmaster.md`。Switch 新代码位于 `switch/`。Vita 版作为补丁和操作参考。Reanimated 当前没有公开核心运行时代码，本项目不包含该运行时。

## 已实现

- devkitA64/libnx AArch64 NRO 工程，SDL2/Mesa/nouveau 图形与音频诊断。
- Xperia Play v1.1.33 游戏库 SHA1 白名单、ELF32 ARM 结构和段边界校验。
- ZIP/APK/已解压 PSV 数据目录导入：临时目录校验后发布，不覆盖目标或修改原始文件。
- Joy-Con 双摇杆和触摸坐标诊断。
- GLES2 纹理三角形回读、上游 PowerVR PVRTC 真实解码器测试、低音量测试音队列。
- 独立 GLES1.1 探测：游戏所需 190 个 GL 名称、matrix-palette 扩展和固定管线三角形回读。
- Dynarmic 执行核心原型：主机实际运行真实游戏库的 5 个短函数，返回值全部正确；Thumb、VFP 与非法执行拒绝测试通过。Switch JIT 适配及含 B 键 CPU 测试的 NRO 已编译，等待实机运行。

用户实机日志已验证 GLES2/PVRTC 和 GLES1 基础三角形；CPU/JIT 和新构造函数测试尚无实机结果。非空函数指针或扩展声明不能证明骨骼渲染正确；音频队列消耗不能代替实际听音。数据检查仅验证必要资源文件存在，不保证章节资源完整。

## 编译

通过 devkitPro 官方包管理安装 devkitA64、libnx、switch-tools、switch-sdl2、switch-mesa、switch-libdrm_nouveau，设置 `DEVKITPRO`：

```sh
make -C switch -j2
```

产物：`switch/build/deadspace-switch.nro`。本次工具版本：GCC 16.1.0、libnx 4.12.0、SDL2 2.28.5、Mesa 20.1.0。编译和链接均需 `-fPIC`，以选择 PIC C++ 运行库。根目录原 Makefile 仍用于 Linux ARM32。

包含 ARM32 执行核心的版本另按 [CPU-BACKEND.md](switch/docs/CPU-BACKEND.md) 编译，需要初始化固定版本的 Dynarmic 子模块、应用 Switch JIT 补丁，并安装 Boost 头文件。默认 Makefile 仍生成较小的平台诊断版。

主机测试（Python3、g++）：

```sh
ASAN_OPTIONS=detect_leaks=0 python3 switch/tests/run_tests.py
g++ -std=c++17 -O2 -Iswitch/include switch/source/preflight.cpp switch/source/host_check.cpp -o ds-preflight
./ds-preflight /path/to/extracted/deadspace
```

ASan/UBSan 用于检查测试中的内存访问和未定义行为。当前容器无法读取 `/proc/.../task`，所以仅 LeakSanitizer 关闭；这不代表完成了泄漏检查。

## PSV 数据导入

用户提供的原版库必须位于 `lib/armeabi/libEAMGameDeadSpace.so`，SHA1 必须为 `0ed42b611415015807f759ec9b5457857143ce39`。

```sh
python3 switch/tools/prepare_data.py /path/to/PSV-data.zip /path/to/sdcard/switch/deadspacemobile/gamedata
```

输出目录必须不存在。工具支持外层含 `ux0/data/deadspace/` 或其他前缀的 ZIP/APK/目录；只复制游戏库和 assets，不执行或转换 VPK。单独的 VPK 启动器不够。RAR/7z 需先解压为目录。必需资源包括 `assets/EAMCore.ini`、`assets/published/data/achievements.sb`。

2026-10-20 用户提供的后续直链 RAR 已完整下载（139,943,803 字节），MD5 为 `3a96fad461ac674b62464349a55d96e1`，与链接标注一致。实际游戏库 SHA1 精确匹配支持版本，1,746 个游戏库与资源文件已安全提取，导入和数据预检通过。章节完整性尚未验证。

真实库审计：2 个加载段，392 个导入符号（含 190 个 GL 名称），69 个 JNI 导出（68 个 Java_* 加 JNI_OnLoad）。主机字节数组中已完成段映射、BSS 清零和 22,677 条 RELATIVE 重定位；上述审计是静态检查。0.3.0 的独立 C++ 构造函数运行时已完成剩余符号重定位与受控导入绑定，实际完成 209 个构造函数和 112 次外部调用；不等于 Switch 实机加载或游戏生命周期。

```sh
python3 switch/tools/audit_donor.py /path/to/libEAMGameDeadSpace.so --output donor-audit.json
```

审计计数已与 binutils readelf 独立比对，190 个 GL 名称与上游清单一致，40 个旧 VFP 补丁位置的原始指令全部匹配。详见 `switch/docs/DONOR-AUDIT.json`。

## 实机诊断

将 NRO 放到 `/switch/deadspacemobile/deadspace-switch.nro`，在已经配置好自制软件的 Switch 上，通过 Homebrew Menu 应用模式/title takeover 启动，给 SDL/Mesa 留足内存。

- A：重扫数据。
- X：GLES2/PVRTC/音频平台测试，约3秒。
- Y：游戏 GLES1.1 固定管线兼容性探测。
- B（0.3.0 CPU 版本）：运行真实库短函数、Thumb/VFP 及全部构造函数，结果写入诊断日志。
- +：退出。
- 摇杆与触摸：检查屏幕上的坐标读数。

日志：`/switch/deadspacemobile/diagnostics.log`。没有游戏文件也能运行平台诊断；本程序不要求密钥、超频或系统配置修改。

## 内置资源构建

最终目标仍为自包含 NSP，无需外部数据文件夹。当前先验证同一 RomFS 资源布局的内置 NRO：

```sh
python3 switch/tools/prepare_romfs.py /path/to/extracted/PSV-data /path/to/new-romfs
make -C switch CPU_PROBE=1 CPU_BUILD_DIR=/path/to/cpu-build ROMFS_ROOT=/path/to/new-romfs
python3 switch/tools/verify_romfs.py switch/build/deadspace-switch.nro
```

先按 CPU 构建说明编译执行核心。RomFS 中内置 `gamedata/`，运行时优先从包内读取。工具会从构建产物逐文件回读并核验 SHA256；本轮 1,746 个原始文件及 1 个导入记录全部一致，共 259,247,598 字节。资源打包验证不代表游戏可玩。

详见 [0.3.0 实测与限制](switch/docs/PROGRESS-0.3.0.md)。

## 下一开发关卡

Dynarmic 主机短函数与构造函数通过。独立 Unicorn 调查已通过 JNI_OnLoad、创建及第一轮 DrawFrame，第二轮停在真实 glGetIntegerv；没有伪造 GL 返回值。该调查尚未移入 Switch C++ 生命周期。下一步验证 Switch JIT，并迁移 JNI/JavaVM、资源流与实际 GL 桥。接通 GLES1、音频、控制和存档后，以第一关可操作、射击、实际声音、存档读档和切关为可玩门槛。详见 `switch/docs/CPU-BACKEND.md`。

当前 NRO 只建立平台诊断能力。没有将 Linux qemu 测试、三角形或占位接口宣称为游戏运行，也没有制作“可玩 NSP”。

## Reanimated 高清包

已核验并单独提取 v1.3 APK 的 222 份 KTX 高清材质，在 APK 自带原始资源中全部找到精确映射。已加入实验性哈希替换、KTX 完整 mip 上传和原图回退；尚未验证 Switch 游戏画面，也未与 PSV 数据逐对象比对。详见 [高清包审计与接入限制](switch/docs/REANIMATED-HD.md)。

## License

GPL-3.0，并保留第三方各自的许可证。见 `LICENSE`、`NOTICE.md` 和 `third_party/`。没有分发 EA 游戏库和游戏资源。
# NSP 状态更新（2026-10-25）

已生成资源内置的独立 NSP 并完成解密回读和内容哈希核验；尚未安装到 Switch 或模拟器。含 EA 资源的 NSP 只在用户本地交付，公开构建方法见 [NSP 记录](switch/docs/NSP-0.4.0.md)。

