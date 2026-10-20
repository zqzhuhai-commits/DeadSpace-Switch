# Dead Space Mobile — Switch 移植开发项目

**0.2.0：ARM32 执行核心原型和 Switch 诊断程序，不是可玩版；游戏生命周期尚未接通，也未通过 Switch 实机测试。**

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

这些平台诊断均已编译，但尚未在 Switch 执行。非空函数指针或扩展声明不能证明骨骼渲染正确；音频队列消耗不能代替实际听音。数据检查仅验证必要资源文件存在，不保证章节资源完整。

## 编译

通过 devkitPro 官方包管理安装 devkitA64、libnx、switch-tools、switch-sdl2、switch-mesa、switch-libdrm_nouveau，设置 `DEVKITPRO`：

```sh
make -C switch -j2
```

产物：`switch/build/deadspace-switch.nro`。本次工具版本：GCC 16.1.0、libnx 4.12.0、SDL2 2.28.5、Mesa 20.1.0。编译和链接均需 `-fPIC`，以选择 PIC C++ 运行库。根目录原 Makefile 仍用于 Linux ARM32。

包含 ARM32 执行核心的 0.2.0 版本另按 [CPU-BACKEND.md](switch/docs/CPU-BACKEND.md) 编译，需要初始化固定版本的 Dynarmic 子模块、应用 Switch JIT 补丁，并安装 Boost 头文件。默认 Makefile 仍生成较小的平台诊断版。

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

真实库审计：2 个加载段，392 个导入符号（含 190 个 GL 名称），69 个 JNI 导出（68 个 Java_* 加 JNI_OnLoad）。主机字节数组中已完成段映射、BSS 清零和 22,677 条 RELATIVE 重定位；其余外部符号绑定仍延后处理。此步骤没有执行 ARM32 指令，也不是 Switch 实机加载。

```sh
python3 switch/tools/audit_donor.py /path/to/libEAMGameDeadSpace.so --output donor-audit.json
```

审计计数已与 binutils readelf 独立比对，190 个 GL 名称与上游清单一致，40 个旧 VFP 补丁位置的原始指令全部匹配。详见 `switch/docs/DONOR-AUDIT.json`。

## 实机诊断

将 NRO 放到 `/switch/deadspacemobile/deadspace-switch.nro`，在已经配置好自制软件的 Switch 上，通过 Homebrew Menu 应用模式/title takeover 启动，给 SDL/Mesa 留足内存。

- A：重扫数据。
- X：GLES2/PVRTC/音频平台测试，约3秒。
- Y：游戏 GLES1.1 固定管线兼容性探测。
- B（0.2.0 CPU 版本）：运行真实库短函数及 Thumb/VFP 测试，结果写入诊断日志。
- +：退出。
- 摇杆与触摸：检查屏幕上的坐标读数。

日志：`/switch/deadspacemobile/diagnostics.log`。没有游戏文件也能运行平台诊断；本程序不要求密钥、超频或系统配置修改。

## 下一开发关卡

实际游戏库和重定位已完成静态核验；Dynarmic 主机执行短函数已通过。下一步验证 Switch 上的 JIT 原型，并接通外部函数调用与 JNI/Bionic。接通 GLES1、音频、控制和存档后，以第一关可操作、射击、实际声音、存档读档和切关为可玩门槛。详见 `switch/docs/CPU-BACKEND.md`。

当前 NRO 只建立平台诊断能力。没有将 Linux qemu 测试、三角形或占位接口宣称为游戏运行，也没有制作“可玩 NSP”。

## License

GPL-3.0，并保留第三方各自的许可证。见 `LICENSE`、`NOTICE.md` 和 `third_party/`。没有分发 EA 游戏库和游戏资源。
