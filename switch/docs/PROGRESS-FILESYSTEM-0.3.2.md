# 0.3.2：修复目录递归、文件 ABI 与带超时的信号量

检查基线：`a12c5322e7e97982c5cfbd020b311980b889d255`。

这是运行时开发修复。真实 donor 已重跑，但当前仍在第二帧的音频接口处中止；没有验证游戏画面、声音、操作或 Switch 实机，也没有生成 NRO/NSP。

## 第二帧故障的更正

基线主机执行最终在 `0x102f7f08` 报 allocator 返回空指针。继续追踪发现，这不是足以证明“allocator interface 没有构造”的证据：此前主线程栈已从 `0x201ffxxx` 一直向下越界，覆盖游戏映像及全局数据。

在栈越界前停止后，调用栈反复出现 `0x10329868`。该位置调用 `0x10329830` 自身：它先调用目录存在检查，检查失败后递归创建父目录。旧 `stat` 用 `fopen` 检测存在性，但始终将 `st_mode` 写为普通文件 `0x81a4`；即使目录存在，游戏依然认为它不是目录，持续向父目录递归。报告中的 allocator 空指针发生在内存破坏之后。

现在按宿主真实 `stat/lstat/fstat` 元数据逐字段编码 Bionic ARM 104 字节结构，保留目录类型并清零 ABI 填充。目录查询不再改变文件读取位置。修复后越过这一递归点，接着补上了实际遇到的 `mkdir`、`sem_timedwait` 和线程调度属性导入。

## 实现范围

- 游戏映像读写使用加载器的段边界和权限；稀疏零页只用于栈、已分配堆和 arena。删除映像到主线程栈之间整片地址空洞的可读写映射。
- 检查运行器返回时的 SP 边界，删除依赖固定地址的 canary 补写。原有 `__stack_chk_guard` 符号绑定仍正常初始化。
- `FILE*` 使用宿主保管的 32 位不透明 token，取消 64 位指针截断；描述符与 stdio 使用独立空间，单调分配避免中间关闭后覆盖其他文件。
- `fread` 返回完整元素个数，保留实际读取的尾部字节；读操作检查长度乘法和 guest 地址回绕。文件错误回写当前 guest 线程 errno，关闭无效句柄报告 EBADF。
- `mkdir` 仅在传入 data root 内创建目录，拒绝 appbundle 写入和根外创建。其余文件服务目前仍限制为只读，尚未实现完整存档。
- `sem_timedwait` 消耗真实信号量或进入调度器等待；`sem_post` 唤醒等待者；到期恢复 -1 和 ETIMEDOUT。空闲时推进到最近期限，不忙等或直接伪造成功。
- `time/gettimeofday/CLOCK_REALTIME` 使用同一 epoch 加虚拟时间，`CLOCK_MONOTONIC` 使用虚拟时间。尚未接入实机帧时钟。
- 线程属性支持 SCHED_OTHER 的零优先级查询和设置；不支持的非零优先级、实时调度策略明确返回错误。

## 本轮真实 donor 结果

恢复用户的 `deadspace.rar`，使用原有导入工具和 SHA1 门槛。库 SHA1 仍为 `0ed42b611415015807f759ec9b5457857143ce39`。未将游戏库或资源上传到 Git。

| 验证阶段 | 观察 |
| --- | --- |
| 叶函数 | 5/5 原始导出、Thumb、VFP 和非法执行拒绝通过 |
| 构造函数 | 209 个构造函数返回，112 次导入调用 |
| JNI 与生命周期 | JNI_OnLoad、audio init、create、surface、visibility 返回，第一帧返回 |
| 第二帧加载 | 55 个真实资源流，向 guest 交付 95,931,619 字节 |
| 后台线程 | 创建 ID 2–6 共 5 个工作线程 |
| 新阻塞点 | `Unsupported Java android/media/AudioTrack.play()V` |

第二帧仍没有返回。上述 audio init 返回不表示实际输出音频，独立图形回读通过也不表示游戏画面通过。

## 回归

- 主机 Release 全目标构建。
- `ASAN_OPTIONS=detect_leaks=0 LIBGL_ALWAYS_SOFTWARE=1 python3 switch/tests/run_tests.py --native-gl` 全部通过，覆盖原有 JNI、内存、线程、纹理解码及 Mesa 回读。
- 新增文件测试：64 位宿主 FILE token、描述符中间关闭重开、独立游标、fstat 游标保持、目录类型与 104 字节 ABI、无效句柄。
- 新增线程测试：超时自动恢复、提前 post 后不再超时、主栈上下边界及地址空洞拒绝。
- `ds-loader-test` 通过真实库的事务绑定、段权限与边界测试。
- `ds-thread-arm-test` 实际执行合成 ARM32 线程，父子等待/唤醒与 join 返回 42 通过。
- Mesa llvmpipe 4.5 compatibility 独立回读通过，11 次 draw；这仍是主机图形桥 fixture。

真实库重跑（仍预期以 AudioTrack 未支持错误退出 1）：

```sh
host-build/ds-cpu-probe \
  /path/to/gamedata/lib/armeabi/libEAMGameDeadSpace.so \
  /path/to/gamedata 10
```

下一关卡是 AudioTrack PCM 服务与实际音频后端，然后继续验证原生 GL 下的完整预加载。矩阵调色板、持续游戏循环、输入、存档、Switch JIT/工具链构建和实机仍待完成。
