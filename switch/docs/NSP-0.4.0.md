# 0.4.0 独立 NSP 构建记录

2026-10-25：已在用户本地生成独立 NSP，265,690,328 字节，Title ID `0100D5ACE0040000`。本记录更新此前“没有 NSP”的阶段状态。没有实机/模拟器安装或运行验证。

程序采用与 0.4.0 候选 NRO 相同 ELF（SHA256 `176bd52ea668ff408940d8084c48f65dbf56c607899822988024837f26f0e7a3`），通过 devkitPro elf2nso 生成 NSO；npdmtool 生成独立应用权限及 39 位地址空间配置，保留 CodeMemory/JIT 所需调用。NACP 使用项目显示名称和用户原图标。资源包全部置于 Program RomFS，不依赖外部 NRO 或手动数据目录。

使用 [hacBrewPack 开源镜像](https://github.com/rlaphoenix/hacBrewPack)，本地 prod.keys 路径仅通过参数传入，不复制进工程。title.keys 不需要用于本次自制应用。未加入任天堂启动动画/标志资源。

修正工具两个兼容问题：Windows ANSI 参数不能传递中文路径，提供 wmain→UTF-8 入口；旧 cnmt.c 将摘要写成零，补丁改为真实 SHA256 摘要。没有修改工具的加密密钥校验来伪造可用包。

## 复现

首先按 REBUILD-0.4.0.md 构建实际游戏 ELF 和私有 RomFS。使用已应用 `switch/patches/hacbrewpack-cnmt-digest.patch` 的 hacBrewPack。

```sh
python3 switch/tools/prepare_nsp.py switch/build/deadspace-switch.elf "$DEVKITPRO" nsp-stage --icon switch/assets/icon.jpg
cd nsp-stage
hacbrewpack -k /local/path/prod.keys --exefsdir exefs --controldir control \
  --romfsdir /absolute/private-romfs --nologo --keepncadir --titleid 0100d5ace0040000
cd ..
python3 -m pip install cryptography
python3 switch/tools/verify_nsp.py nsp-stage/hacbrewpack_nsp/0100d5ace0040000.nsp \
  --keys /local/path/prod.keys --stage nsp-stage --report nsp-verification.json
```

Windows 构建 hacBrewPack 时，使用 LLVM-MinGW，main.c 额外以 `-Dmain=hbp_main` 编译，编译 `switch/tools/hacbrewpack_win_utf8_entry.c`，与工具和 mbedTLS 对象一并使用 `-municode` 链接。此辅助入口只转换参数，不接触或打印密钥内容。

## 实际检查结果

- 三份 NCA：Program（ExeFS 和内置 RomFS）、Control（名称与图标）、Meta（CNMT）。NSP 中没有外置启动目标。
- 独立 Python cryptography 回读通过：NCA AES-XTS 内容头、AES-CTR 分区、内容头哈希、ExeFS 哈希表、RomFS 六层 IVFC、CNMT 文件摘要及两份内容记录。
- 程序、权限、名称/图标与实际打包输入一致；1,748 个文件、259,248,077 字节资源与原清单逐文件一致。
- NSP SHA256：`1d62011be185577aa56e747e5c13b477a1b399eec6b90ab5d2b0c2dc909a8e0a`。

NSP 只保留用户本地输出；公共仓库新增构建/核验代码和本记录，不公开包含 EA 库、资源的安装包，也不公开用户密钥。文件核验不代表实机启动或性能；JIT、声音、帧率、战斗、切关、存档恢复与全流程仍待验证。
