# 0.3.2：真实库启动回归与限时信号量

日期：2026-10-21。基线：`a12c5322e7e97982c5cfbd020b311980b889d255`。
目标继续为 Dead Space Mobile，路径 `deadspacemobile`，最终资源全部内置的独立 NSP。

## 本轮复现与结果

使用此前提供的 `deadspace.rar`，提取后库 SHA1 为
`0ed42b611415015807f759ec9b5457857143ce39`，与生产白名单一致；未修改库或放宽检查。
使用绝对 data_root 调用主机 `ds-cpu-probe`，请求 3 帧：

| 阶段 | 结果 |
| --- | --- |
| 原始 ARM32 叶函数 | 5/5 通过，另有 Thumb/VFP 检查 |
| 原始初始化函数 | 209 个返回 |
| 首个 DrawFrame | 返回 |
| 原始基线第二帧 | 读取 95,929,499 字节后停止于缺失 `sem_timedwait` |
| 增加限时信号量后 | 越过上述停止点，停止于 `pthread_attr_setschedparam` |
| 补齐默认调度参数后 | 创建 worker id 2–6，停止于未实现 `android/media/AudioTrack.play()V` |

本轮未复现先前提交说明中的分配器空指针，不能因此声称该问题已经修复。
先前怀疑的顶层 `Reset()` 寄存器行为未修改。当前复现的最早缺失接口是音频播放。

## 修改

- `sem_timedwait` 使用 ARM32 time32 绝对 CLOCK_REALTIME 截止时间。已有 token 立即成功；无 token 时检查 timespec，过期返回 -1/ETIMEDOUT，非法纳秒返回 -1/EINVAL。
- 调度器保存等待截止时间；其他线程继续运行，无可运行线程时推进虚拟时钟至最早截止点。超时写回等待线程的 r0 和 errno。
- `sem_post` 先处理已过期等待者；超时后的 post 保留为 token。提前 post 则取消该等待者的超时。
- time/gettimeofday/CLOCK_REALTIME 共用启动时捕获的 epoch 加虚拟经过时间；CLOCK_MONOTONIC 使用虚拟经过时间。当前仅支持 clock id 0/1，其他值返回 EINVAL。
- 支持 pthread 的默认 SCHED_OTHER / priority 0 属性与查询；非零优先级/其他策略明确返回错误，不宣称实现原生实时调度。
- 修正心跳日志的指令计数下溢和 64 位资源字节计数格式。

## 验证

- 完整 CMake Release 主机构建通过，使用仓库固定的 Dynarmic 子模块。
- `ASAN_OPTIONS=detect_leaks=0 python3 switch/tests/run_tests.py` 通过；地址及未定义行为检测保持启用。
- `ds-thread-test` 新增限时等待、空闲推进、超时返回值、提前 post 取消、迟到 post 不消费等待者及 errno 隔离测试。
- `ds-thread-arm-test` 通过实际 ARM32 producer/consumer、让出/恢复、join 返回 42，发生 3 次上下文切换。
- `ds-loader-test` 使用真实白名单库通过。
- `git diff --check` 通过。

本轮生命周期回归使用 **mock GL**，资源读取次数/构造函数返回不等于菜单画面或游戏可玩。
未交叉编译 Switch、未运行实机、未验证音频/输入/存档/关卡；未生成 NSP。

## 下一步

实现真正的 AudioTrack 状态与 PCM 输出后继续回归，不以空实现 `play()` 越过停止点。
随后仍需接通并验证真实渲染、必要的条件变量/其他 pthread 服务、输入与持久化，最终完成独立 NSP 安装验收。
