# 持续运行前端、构建回归与独立 NSP 打包路径

日期：2026-10-22。开发基线：PR #2 / `a2e73c49349a68854472fac70b457d3d353e31c8`。

## 本轮修复

- 原 R 键原生 GL 探测仍为 8 帧；新增 ZR 持续运行前端，SDL Start（Switch +）和窗口关闭可结束会话，Horizon applet 生命周期也会检查。实际返回的帧数不再被请求帧数覆盖。
- 将生命周期 trace 改为保留最近 512 条，每条最多 2,048 字符。此前每次 Java/GL/DrawFrame 都追加字符串，会随长时间运行增长。报告包含丢弃条数；此改动不代表完成全部运行时内存泄漏审计。
- 修复 `romfs:/gamedata/...` / `sdmc:/.../gamedata/...` 被再次拼接数据根目录的问题，保留相对资源与 appbundle 路径解析；拒绝访问其他数据根目录。
- 删除每次资源打开时写 `/tmp/open_log.txt` 的主机调试副作用。
- Makefile 根据 CPU/平台、独立/诊断和版本/Title ID 变化更新配置戳；CPU 静态库列为链接依赖。切换模式或重编执行核心后不会继续使用旧对象/旧 ELF。
- 新增 `STANDALONE=1 CPU_PROBE=1` 独立应用入口：只接受包内 `romfs:/gamedata`，预检通过后直接进入持续 GL/PCM/触摸会话；启动失败显示错误。不会以外置 NRO 作为启动目标。
- 新增 `elf2nso` 目标，以及本地 `package_nsp.py`：资源导入/白名单与清单校验、独立 NRO 回读、NSO/NPDM/NACP/control、全新 hacBrewPack 临时目录、NSP PFS0 结构校验和拒绝覆盖输出。

## 自动工作流

此前仓库没有 `.github/workflows`，Actions 查询也没有运行记录。本轮新增 `.github/workflows/switch.yml`：

1. Ubuntu 主机：ASan/UBSan、真实 Mesa 像素回读、合成资源工具测试、增量构建模式切换、打包格式负面测试、完整 Dynarmic/媒体前端编译，以及 CTest。
2. devkitPro 容器：独立 RomFS 写入/回读测试、平台 NRO、CPU 诊断 NRO和独立 NSO 交叉编译。

`hd_pack_test.py` 依赖本地真实高清包，不作为无数据公开 CI 用例。`romfs_test.py` 必须传入 `build_romfs` 路径，因此在安装 Switch 工具的 job 中执行。公开工作流只构建源码，不接收游戏文件或 keyset，不自动发布安装包。

## 本地构建命令

先初始化固定依赖并构建 Switch CPU 核心：

```sh
git submodule update --init --recursive
export DEVKITPRO=/opt/devkitpro
export BOOST_ROOT=/usr
bash switch/tools/build_cpu_probe.sh
```

随后准备经过审查的独立应用 NPDM JSON（AArch64、应用内存配置、实际图形/音频/SD/RomFS 服务及 libnx JIT 所需权限），使用自己选定且未占用的 base application program ID。JSON 的 `name` 必须为 `deadspacemobile`，`program_id`（或旧 `title_id`）及其 min/max 范围必须相同。脚本检查身份一致性，不宣称已验证 NPDM 权限足够。

```sh
python3 switch/tools/package_nsp.py \
  --data /path/to/original-supported-data \
  --cpu-build /absolute/path/to/switch/build/cpu \
  --npdm-json /path/to/reviewed-application.json \
  --keyset /path/to/local-keyset \
  --hacbrewpack /path/to/hacbrewpack \
  --output /path/to/DeadSpace-Mobile-development.nsp
```

需要高清资源时额外传 `--hd-apk /path/to/Reanimated-v1.3.apk`，继续使用现有精确匹配、原图回退和显存预算。keyset、原始库、游戏资源和生成的 NSP/NCA 不纳入 Git。打包接口依据 [hacBrewPack README](https://github.com/pplatoon/hacBrewPack/blob/master/README.md) 及 [switch-tools](https://github.com/switchbrew/switch-tools)；临时目录每次独立创建并清理。

## 本轮验证与交付限制

- ASan/UBSan 主机套件和 llvmpipe 原生 GL 像素回读通过。
- 合成高清 RomFS 组合与 KTX/映射/导入回滚测试通过。
- 实际 SDL2 事件队列测试通过：触摸坐标、退出键拦截、窗口关闭。
- 增量构建回归通过：不变构建不重编、平台/CPU/独立入口切换正确、CPU 静态库更新触发重新链接。
- NPDM 标识一致性与 NSP 截断、越界、重名、路径及内容重叠拒绝测试通过。这些是合成格式测试，不是实际 NSP 安装测试。
- 完整主机 Dynarmic/生命周期/媒体前端编译通过；CTest 7/7 通过，含实际 ARM32 协作线程、JNI、内存、SDL 输入、高清和原生 GL。
- 使用 switch-tools 独立 RomFS 写入器构建的资源回读/损坏拒绝测试通过。
- 本轮未取得匹配原始数据及高清包，未运行真实 donor、未验证关卡。
- 本轮尚未完成 Switch 实机与独立 NSP 安装；没有可交付的完整 NSP。

独立程序和打包通路已补入，但运行时仍有重要缺口：guest 文件服务只支持只读访问，尚未将 GetAppDataDirectory/存档写入分离到可写存储；模拟摇杆/体感替代、游戏菜单和关卡控制也未验收。只读 RomFS 无法代替存档实现。仍需真实库启动、第一关操作/瞄准/射击、实际听音、存档重启读档及切关验证，才能满足 RELEASE-REQUIREMENTS.md。
