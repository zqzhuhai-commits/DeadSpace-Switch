# Dead Space Mobile — Switch 移植开发项目

目标是内置游戏库和资源、无需外部 NRO 或手动数据目录的单文件 NSP。当前为开发版本：主机桥接测试与交叉编译不等于 Switch 安装、完整关卡或实际听音验收。

代码位于 `switch/`。原 Linux/PortMaster 项目说明保留在 [README.portmaster.md](README.portmaster.md)。上游来源与第三方许可证见 `NOTICE.md`、`switch/docs/UPSTREAM.json` 及 `third_party/`。

## 当前源码能力

- devkitA64/libnx 独立应用与 NRO 诊断入口，使用 SDL2/Mesa/nouveau。
- 固定版本 Dynarmic ARM32 执行、受控 ELF 映像、JNI、资源流、协作线程及原生 GLES 子集。
- AudioTrack PCM 到 SDL 队列、触摸和基础数字按键。模拟摇杆、体感替代及完整游戏控制仍待完善。
- 只读数据与可写存档分离；实现 guest open 标志转换、读写、刷新、重命名和删除。
- 独立应用 NPDM、NSO 输出与本地 NSP 打包脚本；支持从 CI 编译的独立 NSO 进行本地资源打包。
- 合成文件持久化、内存/JNI/线程、原生 GL 像素回读、资源导入与包格式回归。

源码与公开 CI 不包含游戏资源、keyset、用户存档或私有实测报告。应用名称为 **Dead Space Mobile**，图标使用 `switch/assets/icon.jpg`。开发标识和存储目录统一为 `deadspacemobile`。

## 获取当前开发源码

```sh
git clone --recurse-submodules --branch codex/fix-filesystem-preload \
  https://github.com/zqzhuhai-commits/DeadSpace-Switch.git
cd DeadSpace-Switch
```

GitHub 自动生成的源码 ZIP 不包含 Dynarmic 子模块内容；构建请使用递归克隆，或单独取得 `.gitmodules` 指定的固定依赖。当前开发工作尚未合并到主分支。

## 编译

安装 devkitA64、libnx、switch-tools、Switch SDL2/Mesa/nouveau，以及 CMake、Ninja 和 Boost 头文件。设置：

```sh
export DEVKITPRO=/opt/devkitpro
export BOOST_ROOT=/usr
bash switch/tools/build_cpu_probe.sh
```

脚本会隔离 Boost 头文件，避免把宿主 `/usr/include` 带进 Switch 交叉编译器。不要直接将宿主系统头目录加入目标编译选项。

独立程序与 NPDM：

```sh
make -C switch CPU_PROBE=1 STANDALONE=1 build/deadspace-switch.nso
npdmtool switch/package/npdm.json switch/build/main.npdm
```

独立入口仅接受包内 `romfs:/gamedata`。不带执行核心的平台诊断仍可用 `make -C switch` 构建；根目录 Makefile 用于原 Linux ARM32 目标。

## 数据准备与资源内置

导入器只接受受支持的原版游戏库及资源，检查位于 `switch/tools/prepare_data.py` 的版本白名单；不通过校验的数据不会发布到目标目录。

```sh
python3 switch/tools/prepare_data.py /path/to/extracted-data /path/to/new-gamedata
python3 switch/tools/prepare_romfs.py /path/to/new-gamedata /path/to/new-romfs
```

RAR/7z 请先解压为目录；导入器直接支持目录、ZIP 和 APK。独立 VPK 启动器不包含完整游戏数据。输出目录必须不存在，原始输入不会被覆盖。

内置 NRO 的开发检查：

```sh
make -C switch CPU_PROBE=1 ROMFS_ROOT=/path/to/new-romfs
python3 switch/tools/verify_romfs.py switch/build/deadspace-switch.nro
```

资源从包内 RomFS 读取。运行产生的存档写入 `sdmc:/switch/deadspacemobile/saves`；不需要预先复制外部游戏资源目录。

## 打包单文件 NSP

在本地准备 hacBrewPack、switch-tools、对应源码版本的独立 NSO 和本地 keyset：

```sh
python3 switch/tools/package_nsp.py \
  --data /path/to/new-gamedata \
  --prebuilt-nso /path/to/CI/deadspace-switch.nso \
  --npdm-json switch/package/npdm.json \
  --keyset /path/to/local-keyset \
  --hacbrewpack /path/to/hacbrewpack \
  --output /path/to/DeadSpace-Mobile-development.nsp
```

也可以用 `--cpu-build /absolute/path/to/switch/build/cpu` 替代 `--prebuilt-nso`，在本机重新交叉编译。脚本校验资源、独立回读 RomFS、组装 NSO/NPDM/NACP/control，并检查 NSP 的 PFS0 结构；临时目录每次独立创建，已有输出不会被覆盖。

NPDM 的应用标识、服务及 JIT 配置见 `switch/package/npdm.json`。安装和启动仍需要在实际设备上验证。包结构检查不能代替关卡可玩验收。

## 主机测试与 CI

```sh
ASAN_OPTIONS=detect_leaks=0 LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless \
  python3 switch/tests/run_tests.py --native-gl
python3 switch/tests/package_nsp_test.py
python3 switch/tests/build_modes_test.py
```

原生回读需要主机 EGL/GL 运行库。`romfs_test.py` 需要传入独立 `build_romfs` 工具路径；真实高清包对照测试需要另行提供本地资源，不属于无资源公开 CI。

`.github/workflows/switch.yml` 执行主机回归、运行时/媒体前端构建、CTest，以及固定官方 devkitPro 镜像中的交叉编译和开发产物上传。公开工作流只接触源码与合成测试数据。

## 开发诊断

NRO 诊断入口：R 为原生 GL 短探测，ZR 为持续会话，+ 结束会话。独立程序直接进入包内资源会话。诊断日志为 `sdmc:/switch/deadspacemobile/diagnostics.log`。

可写文件桥接和显式 EAIO 启动的实现说明见 [PROGRESS-SAVE-NSP-0.3.2.md](switch/docs/PROGRESS-SAVE-NSP-0.3.2.md)。仍需验收第一关移动/瞄准/射击、实际听音、完整进度恢复、切关及无外置数据安装；开发包不应标为已完成这些验收的可玩整合版。

## 高清替换

可选 `prepare_romfs.py --hd-apk` 使用精确纹理键匹配、原图回退和显存预算。高清来源与映射支持范围见现有开发文档；源码不附带高清材质。

## License

GPL-3.0，并保留第三方各自许可证。见 `LICENSE`、`NOTICE.md` 与 `third_party/`。
