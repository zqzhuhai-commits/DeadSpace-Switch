# 0.3.2：PCM 桥接、匿名映射回收及真实启动画面

日期：2026-10-21。父提交：`4c1f3a5e4a41c6ce6ffaea86dadce9b33cebcfc5`。
目标保持 Dead Space Mobile、`deadspacemobile` 路径，以及最终资源全部内置、安装即启动的独立 NSP。

## 本轮修复

### 音频

- `AudioTrack` JNI 处理器支持 PCM16 的 `short[]` / `byte[]` 写入、对应样本/字节返回值、偏移与数组边界、完整声道帧校验、部分写入，以及播放/暂停/停止/释放状态。
- 音频后台缺失、设备打开失败、队列错误均明确停止，不用空实现伪造播放成功。
- Switch 的 R 原生图形入口现在把游戏 PCM 交给 SDL 音频设备；队列有容量限制，阻塞写入等待独立 SDL 消费线程，超过 2 秒未排空会停止诊断。
- 主机可选 WAV 捕获真正接收游戏交付的 PCM，以最终 WAV 头和样本统计验证。WAV 后台明确不代表扬声器播放；捕获最大 16 MiB，拒绝覆盖现有文件。

### 真实库启动中发现的后续阻塞

1. `qsort`：增加受控 guest 比较函数回调。比较函数接收 guest 数组内地址；排序完成后才写回排列，回调失败不发布部分排列。限制元素数、总长度、地址溢出及回调数。
2. 字体增长分配失败：原 `munmap` 恒返回成功，未释放地址；诊断发现 break `0x41001000` 与 mmap 边界 `0x41063000` 几乎相遇，随后字体缓冲区向 null 复制。
3. 新 `GuestArena` 保持原来的 **256 MiB** 区域，回收/复用映射空洞，支持部分解除映射，释放实际页缓存，并校验 sbrk 与 mmap 的边界。没有通过扩大内存限制绕过问题。
4. `tanf`：补齐软浮点 ABI 的实参/返回值计算。
5. 固定点 GL：补齐纹理参数与环境标量、颜色、矩阵、变换及部分深度/线宽接口；区分整数枚举与 16.16 数值。未支持的 GL 调用仍明确停止。
6. 错误报告加入当前服务、LR、匿名映射边界、JNI 堆及最后资源路径，非法 memcpy 地址在复制前拒绝。

## 已获得的主机证据

使用既有原始数据，库 SHA1 仍为 `0ed42b611415015807f759ec9b5457857143ce39`。

真实原始库 + Mesa llvmpipe 桌面兼容 GL 的 8 帧回归：

- 209 个原始构造函数返回；JNI、audio init、create、surface、visibility 返回。
- **8 个 DrawFrame 全部返回**。
- 5,065 次 GL 调用，407 次绘制提交，130 次纹理上传，其中 122 次 ATC 上传。
- 帧缓冲回读 54,823 个非黑像素，GL 错误码 0；目视检查可见 **EA 启动画面**。这不是主菜单或关卡验收。
- 游戏提交 137,216 字节 PCM、67 次写入；当前样本全部为零。只能证明数据路径接通，尚无游戏声音正常的证据。
- 约 149.5 MB 累计资源读取是流量统计，不能当作常驻内存或已验证完整章节资源。

主机 pbuffer 为 1280×720；这不等于游戏 viewport 已覆盖全屏，更不是 Switch 实机截图。测试未开启高清替换包。

扩展到 600 帧的真实 GL 回归亦完整返回：404,665 次 GL 调用、34,743 次绘制、130 次纹理上传，GL 错误码 0，回读 60,375 个非黑像素。累计读取 152,990,519 字节，图像仍为 EA 启动画面，不能宣称已到主菜单。

600 帧 PCM 捕获为 438,272 字节、214 次写入，其中 97,534 个非零样本、峰值 26（PCM16 满量程 32,768）。这说明已收到很低幅度的非静音数据，**不能认定声音正常或达到正常响度**；未进行听音验收。

进一步扩展到 **3,000 帧** 的真实 GL 回归也全部返回：

- 2,017,258 次 GL 调用、173,640 次绘制、132 次纹理上传，其中 122 次 ATC 上传。
- GL 错误码 0，帧缓冲回读 305,842 个非黑像素。
- 目视检查已由 EA 标志推进至带纹理和光影的 **3D 走廊场景**。尚无菜单操作或可控角色验收，不把背景/启动场景称为可玩关卡。
- 1,368,064 字节 PCM、668 次写入、539,291 个非零样本，峰值 404/32,768，仍不能认定正常音量。
- 当前图像落在 1280×720 pbuffer 的部分区域，视口/显示尺寸仍需修正。没有通过截图缩放掩盖这个问题。

机器可读结果见 [BOOT-AUDIO-GL-0.3.2.json](BOOT-AUDIO-GL-0.3.2.json)。

## 重现方式

先初始化固定 Dynarmic 子模块，主机需 CMake、Boost 头文件、g++，原生回读另需 EGL/桌面兼容 OpenGL：

```sh
cmake -S switch/runtime -B switch/build/host -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build switch/build/host --parallel 4
switch/build/host/ds-cpu-probe /path/to/library.so /path/to/data 8 /tmp/new-capture.wav
switch/build/host/ds-game-gl-probe /path/to/library.so /path/to/data 8 /tmp/new-capture.ppm /tmp/new-native.wav
ASAN_OPTIONS=detect_leaks=0 python3 switch/tests/run_tests.py --native-gl
```

`ds-game-gl-probe` 的截图/WAV 是可选本地诊断输出，不包含在源码库内；PCM 路径必须不存在。

## 验证与限制

- 完整主机 CMake Release 构建；ASan/UBSan JNI、内存、线程、匿名映射、排序、WAV/PCM、纹理解码和真实 GL 回读测试通过。LeakSanitizer 关闭。
- 新匿名映射测试涵盖部分 unmap、空洞复用、1,000 次 200 MiB 映射/解除映射、sbrk 碰撞与地址/页对齐溢出。
- PCM 测试涵盖 JNI 重载、负偏移/越界/声道帧、样本与字节数量、部分写入、生命周期、-32768 峰值及实际 WAV 文件内容。
- SDL dummy 设备实测打开、限量排队、阻塞排空、暂停/清空/关闭通过；不代表听到了声音。
- `game_graphics_probe.cpp` 以 SDL 头文件及 `-Wall -Wextra -Werror` 主机语法检查通过。
- 本轮未安装配置 devkitPro，因此 **没有 Switch 交叉编译产物，没有实机验证，也没有 NSP**。

后续仍需主菜单/关卡、正常声音与响度、实际输入、存档、矩阵调色板等设备兼容性及自包含安装验收，才能标记可玩完整版。
