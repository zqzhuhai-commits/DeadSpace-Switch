# 可写存档桥接与独立 NSP 构建

## 源码变更

- `GuestStoragePaths` 分离只读包内资源和可写目录。Switch 使用 `sdmc:/switch/deadspacemobile/saves`，Java 数据目录委托返回该目录。AssetManager 和 appbundle 继续读取只读数据根目录。
- 文件服务显式转换 Bionic ARM open 标志，实现 fwrite/write、fflush、重命名和删除；descriptor 不使用宿主 stdio 缓冲。
- EAIO 递归创建目录前可以查询所选数据/存档根目录的父目录元数据。该查询不会授权 fopen 或修改父目录，也不会授权访问其他游戏目录。
- 文件回归采用合成数据，覆盖独占创建、追加/刷新、关闭后新 runtime 对象重读、无效 flags、路径越界及只读资源边界。
- 媒体探测器增加最后一个 `save_root` 参数；目录须预先存在。未指定时保留只读诊断模式。
- 整合远端 `8c4a7d8` 的显式 EAIO_Startup，位于 JNI_OnLoad 后、audio/Create 前。
- 生命周期报告包含存档文件打开次数和实际读写字节计数，供本地联调检查。
- 独立应用 NPDM 使用名称 deadspacemobile，Application 类型、64 位地址空间、内容读取/SD 卡权限及 libnx JIT 所需调用。
- 本地 NSP 工具支持 `--prebuilt-nso`，可使用对应源码版本经 CI 编译的独立 NSO。独立 build_romfs 写入后按清单回读，再打包为 NSP。

## 本地命令

```sh
mkdir -p /path/to/save-root
SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless \
  switch/build/host/ds-media-probe /path/to/gamedata/lib/armeabi/libEAMGameDeadSpace.so \
  /path/to/gamedata 1000 /path/to/no-hd frame.ppm "" /path/to/save-root

python3 switch/tools/package_nsp.py --data /path/to/gamedata \
  --prebuilt-nso /path/to/CI/deadspace-switch.nso \
  --npdm-json switch/package/npdm.json --keyset /path/to/local-keyset \
  --hacbrewpack /path/to/hacbrewpack --output /path/to/new-development.nsp
```

原始资源、keyset、用户生成的存档、资源校验信息和真实库实测报告不纳入公开源码。公开 CI 只使用源码及合成测试数据。代码编译与文件桥接回归不替代 Switch 安装、第一关控制、实际听音、完整进度恢复及切关验收；开发 NSP 不应被标记为已完成这些验收的可玩整合版。
