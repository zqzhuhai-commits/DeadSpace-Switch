# 0.3.0 实测与限制（2026-10-20）

当前交付是内置资源的开发诊断 NRO。**不是可玩完整版，也不是 NSP。** 最终交付要求保持为自包含 NSP；程序与资源均内置，不依赖外部 NRO 或手动复制的游戏数据目录。

## 本轮实际执行

### C++ / Dynarmic

SHA1 固定的原始库在主机执行了全部 **209 个构造函数、112 次外部调用**。同一代码已加入 Switch 静态运行库及 B 键测试。

新增有边界的符号重定位、受控 ARM32 SVC 导入桥、稀疏客体内存、分配器 arena、sbrk/匿名 mmap、互斥量属性与所有权、信号量、原子操作、时钟及退出回调登记。客体地址始终是整数，不转成主机函数指针。未知导入、映射外访问、需要调度器的等待均停止并报告。

导入绑定先完整解析，再修改镜像；未解析或重复绑定均拒绝。加载器 ASan/UBSan 检查通过，包括失败绑定不改变镜像、重复绑定、越界/溢出访问、只读段写入拒绝和失败重载清理。另保留 5 个真实短函数、Thumb/VFP 和非法执行测试。

**限制：** 此 C++ 运行时目前仅单线程执行构造函数，不是完整 Bionic。free 不回收 arena；退出回调只登记。JavaVM/JNI、生命周期和实际游戏线程尚未接通。Switch JIT 仍无实机运行结果。

### 独立主机启动调查

`switch/tools/boot_probe.py` 使用 Unicorn，不属于 Switch 运行时。真实执行顺序：

1. 全部 209 个构造函数返回。
2. JNI_OnLoad 返回 `0x10004`。
3. 实际 EAAudioCore_Init、NativeOnCreate、NativeOnSurfaceCreated 返回。
4. 首次 NativeOnDrawFrame 返回，并从供应数据实际读取 `assets/EAMCore.ini`。
5. 第二次 NativeOnDrawFrame 在尚未实现的 `glGetIntegerv(GL_VIEWPORT, ...)` 停止。

没有用 GL 成功占位函数绕过阻塞，没有画面或游戏音频输出，也没有游戏 FPS。虚拟手机属性只供启动调查，传感器和保持唤醒请求仅记录；JNI 引用不回收，不是生产实现。资源就绪只检查必要哨兵文件，不能证明全部章节完整。

复现（需本地 Unicorn 2.1.4、Linux libm 和用户自己的游戏数据）：

```sh
python3 switch/tools/boot_probe.py /path/to/libEAMGameDeadSpace.so /path/to/data --stage frame --frames 120 --output boot.json
```

预期在真实 GL 导入处以退出码 2 停止；不是 120 帧成功。证据见 `BOOT-HOST-0.3.0.json`。

### 内置数据

新增 `prepare_romfs.py` 和 `verify_romfs.py`。前者安全导入原始库与 assets，再生成逐文件清单；后者独立解析最终 NRO/RomFS，逐文件回读 SHA256，核对文件集合和字节数。独立 devkitPro RomFS 写入器的测试验证了正确镜像通过、文件损坏和元数据截断被拒绝。

本轮打入 1,746 个原始文件和 1 个导入记录，合计 **259,247,598 字节**；最终 NRO 回读全部匹配。游戏名和用户图标已嵌入，程序优先选择 `romfs:/gamedata`。此测试包无需另复制 PSV 数据。

## 真机测试用途

以 Homebrew Menu 的应用模式/title takeover 运行内置资源 NRO，按 B 执行 CPU/构造函数测试。记录预检是否选中 `romfs:/gamedata`、是否出现 `PASS: 209 actual game constructors returned`。日志在 `sdmc:/switch/deadspacemobile/diagnostics.log`。

没有真机测试就不能确认 libnx JIT 权限、缓存同步、运行内存和跨架构服务桥实际可用。平台图形/PVRTC 的既有日志成功不代表新 CPU 运行时成功。

## 完整版仍需完成

- 将已调查的 JNI/JavaVM 和生命周期迁入 C++，接入客体线程、同步及退出。
- 接入真实 GLES 调用和四个缺失的 matrix-palette 接口；验证人物骨骼和场景，不以三角形代替。
- 接入游戏 PCM 音频、Joy-Con/触摸、资源与可写存档分流。
- 完成第一关可操作、射击、听音、存取档和切关实测，再生成并验收自包含 NSP。

原始库和资源仅用于用户自己的测试包，不进入公开 Git 仓库。
