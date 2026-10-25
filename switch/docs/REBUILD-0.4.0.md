# 0.4.0 重制上机候选版

后续更新：已生成独立 NSP 并完成解密和内容核验，见 [NSP 构建记录](NSP-0.4.0.md)。本文中“没有 NSP”为此前 NRO 交付阶段的状态，实机/模拟器验证仍未进行。

日期：2026-10-25。基线：`48fcea2663a7b87e5ad387015ab910953b6275c2`。

本分支的新入口直接执行真实游戏，默认关闭高清替换。Windows 主机已进入简体中文第一关，并验证角色移动和视角输入。它是上机测试候选版；Switch 实机、Switch 模拟器、全流程、切关和实机帧率尚未验证。本轮没有 NSP。

## 修复内容

- 独立 ARM32 文件句柄，消除 64 位 FILE 指针截断、关闭后描述符碰撞、fstat 改变读偏移和 fread 元素计数错误；文件操作有边界和错误返回。
- 真正区分文件和目录。旧 stat 用 fopen 判断目录，导致根目录递归和栈溢出；现在使用实际文件元数据并清零完整 guest 结构。
- 资源保持只读，`/data` 指向独立存档；`appbundle:` 和 `/published` 指向实际 assets。首次存档可以从资源包内已校验的中文设置初始化，已有设置不被覆盖。
- mmap/munmap 跟踪存活区间、部分释放及空洞复用，不再把成功返回当成释放。旧版本会耗尽 256 MiB guest 区域，随后向空指针复制资源。
- 分配器使用可用块大小索引，避免每次分配扫描全部已分配对象。十万次分配/释放复用压力检查通过。
- 可写完整页通过 Dynarmic 页表快速访问；跨页访问回退检查，munmap 和线程栈退出/复用时撤销指针。代码/非完整页不开放同样的快速写入。
- JNI 资源和 GL/内存复制按页批量处理；openFd 只取元数据，close 释放资源缓存；Java 调用追踪仅保留首次签名。
- 定时信号量有真实超时/取消语义；实际图形运行采用真实时钟，诊断可保留虚拟时钟。主渲染线程返回后的短时间片让后台加载和音频工作线程继续执行，避免永远停在耳机页。
- 按上游已核验的调用点补齐 `/published` VFS 挂载，仍调用真实 mount 函数遍历实际数据；没有强写任务完成标志。
- 补上真实 qsort 比较回调、tanf、strtod 及实际遇到的 GLES 定点颜色/矩阵/光照/纹理参数桥。
- AudioTrack 送入 SDL/WinMM 实际队列；队列满时挂起 guest 写线程，不以静默丢弃模拟播放。
- 新增直接游戏启动入口、Joy-Con/Pro 手柄和触摸映射、菜单指针及断连释放；补上暂停/停止生命周期调用。
- 屏幕自然方向尺寸与实际 GL drawable 匹配。新入口请求 960×540，实际尺寸记录到日志；不以请求尺寸当成硬件实际结果。

## 本轮已运行

环境：Windows x86-64，RTX 3060 Laptop GPU，LLVM-MinGW 23.1.4；目标工具链 GCC 16.1.0、libnx 4.12.0、SDL2 2.28.5、Mesa 20.1.0-5。

- 原始 7z SHA256 `5edd7d9d9c91e6d379eeb572f5bc025e1a2f817c00adf27cb3d2f16d595f234d`，未改动。
- 内层 ZIP 145,903,794 字节，SHA1 `61d51d8ba5374f97b5a4971a2d9d7da31baf840c`；游戏库为支持的 Xperia Play v1.1.33，SHA1 `0ed42b611415015807f759ec9b5457857143ce39`。
- Unicorn 主机调查执行 209 个构造函数、JNI 和首帧，随后在未接入的 GL 查询处明确停止；没有把它称为 Switch 模拟器验证。
- Dynarmic C++ 主机执行 5 个真实导出、Thumb/VFP、构造函数和实际游戏生命周期。
- 主机真实 OpenGL 连续 4,200 帧：2,177,681 次 GL 调用、167,764 次绘制提交、529 次纹理上传（498 次 ATC）。实际帧显示中文第一关；同一 ControllerInput 映射推动角色移动和转动视角，教程从“动作控制”推进到“查看控制”。这不是 Switch 帧率数据。
- PCM 实际送入播放设备；另一次 700 帧检查记录 963,584 个样本，其中 846,512 非零，峰值 3,673。未人工听音，未验证 Switch 声音质量/同步。
- 实际创建设置、成就、关卡和检查点文件；文件层写入、关闭、重开读回测试通过。游戏完整关卡进度恢复仍待验收。
- 中文难度页显示“选择难度 / 正常 / 简单”，字形完整。初次只改语言字符串时校验失败，现已识别 SBIN 各段 FNV-1 校验并正确更新；正常重启仍保持中文。
- ASan/UBSan 主机套件：JNI/ABI、真实资源流、内存/堆、线程/定时等待、压缩纹理、文件、存储、匿名映射和控制映射通过。
- 实际 ARM 指令测试：后台线程等待/唤醒/join；快速页读写、撤销及跨页保护回退通过。
- 独立 RomFS 写入/回读、内容改动和截断拒绝测试通过。
- 实际 GL 像素、定点带符号矩阵、光照向量和纹理枚举读回通过。
- Switch AArch64 执行核心及直接游戏入口交叉编译/链接通过。最终资源包另由 verify_romfs.py 逐文件回读核验。

可选高清组合测试在 Windows 创建符号链接时被系统权限拒绝，没有宣称该组合测试通过。候选版不启用高清替换。

## 构建

使用固定 Dynarmic 子模块及官方 devkitPro。设置 DEVKITPRO 和 Boost 头文件路径（测试为 Boost 1.83）：

```sh
git submodule update --init --recursive
git -C third_party/dynarmic apply ../../switch/patches/dynarmic-switch-jit.patch
git -C third_party/dynarmic apply ../../switch/patches/dynarmic-host-compat.patch
cmake -S switch/runtime -B cpu-build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/switch/runtime/switch-toolchain.cmake" \
  -DBoost_NO_BOOST_CMAKE=ON -DBOOST_ROOT="$BOOST_ROOT" \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build cpu-build --parallel 4
make -C switch -B -j4 CPU_PROBE=1 GAME_LAUNCHER=1 \
  CPU_BUILD_DIR="$PWD/cpu-build" APP_VERSION=0.4.0-rebuild \
  APP_ICON="$DEVKITPRO/libnx/default_icon.jpg"
```

host-compat 补丁只包含已公开依赖的构建兼容修正；原始依赖许可证保留。Windows CMake 工具链自动选择 .exe 编译器。LLVM 23 主机构建另使用 `-DCMAKE_CXX_FLAGS="-DFMT_CONSTEVAL= -include vector" -DFMT_OS=OFF`；不将这个主机参数误用于目标编译。

准备私有整合包（settings.sb 必须来自自己的游戏生成文件）：

```sh
python3 switch/tools/prepare_romfs.py donor.zip new-romfs --settings settings.sb
make -C switch CPU_PROBE=1 GAME_LAUNCHER=1 CPU_BUILD_DIR="$PWD/cpu-build" \
  APP_VERSION=0.4.0-rebuild APP_ICON="$DEVKITPRO/libnx/default_icon.jpg" \
  ROMFS_ROOT="$PWD/new-romfs"
python3 switch/tools/verify_romfs.py switch/build/deadspace-switch.nro
```

公开运行时不含 EA 游戏库、资源和用户设置。资源内置 NRO 仅作为用户本地输出；未上传公共仓库。没有可用的 NSP 打包配置/密钥材料，因此不创建假 NSP 或以转发器冒充完整安装包。

## 上机控制

| 输入 | 行为 |
|---|---|
| 左摇杆 / 右摇杆 | 移动 / 视角；明显推动摇杆后隐藏菜单指针 |
| ZL / ZR | 瞄准 / 射击 |
| A | 指针显示时点击；隐藏时交互 |
| B | 返回 |
| X / Y | Xperia Play X / Y 动作（换弹、特殊动作依上下文） |
| L / R | 上一个 / 下一个武器 |
| + / - | 暂停 / HUD；同时按 + 和 - 退出 |
| 右摇杆按下 | 显示/隐藏菜单指针 |
| 十字键 | 指针显示时移动指针；隐藏时上/下发送模式切换/Zero-G 手势 |
| 触摸 | 菜单直接点击 |

Zero-G 和武器模式使用上游/Vita 参考的加速度序列；已经接入和检查事件序列，未到相关关卡实测。射击事件已送入游戏，但伤害、命中、特殊武器和完整键位舒适度仍待实机验收。

## 仍需实机和全流程验证

- libnx JIT、原生 GLES1、PCM 声音和系统缩放是否在目标机正常工作。
- 正常频率下第一关/战斗/切关的帧间隔、音画同步和内存峰值；没有实机 FPS 承诺。
- 游戏进度读回、切关、后续 Boss/Zero-G 和长时间稳定性；本轮没有全通关。
- 尚未实现完整 Android pthread 条件变量/调度策略和完整 JNI 引用回收；遇到未支持调用会停止并记录，不伪装成功。
- 主机截图只是主机证据；未运行 Switch 模拟器或硬件。

输入和初始化参考保留自 EapRules/deadspace-native-arm 与 [Vita 移植项目](https://github.com/v-atamanenko/deadspace-vita)，参见仓库 NOTICE.md 和各依赖许可证。
