# 2026-10-20 实机日志分析

来源：用户上传的 diagnostics.log。没有把未记录的项目视为通过。

| 项目 | 日志证据 | 判断 |
| --- | --- | --- |
| 原版库 | Missing: lib/armeabi/libEAMGameDeadSpace.so | 预检失败，当前运行没有读到预期路径的库 |
| CPU 核心 | 没有 ARM32 CPU probe 结果 | 本日志不能验证 Dynarmic 在 Switch 的执行情况 |
| GPU | GL renderer: NV120，GL error=0x0 | 基础上下文和已执行图形测试正常 |
| GLES2 | shaders linked；textured triangle framebuffer readback OK | 着色器与纹理三角形通过 |
| PVRTC | decoder fixture OK | 已测解码样本通过；不等于全部游戏纹理验证 |
| GLES1 固定管线 | fixed-function triangle readback OK | 基础固定管线绘制通过 |
| 矩阵调色板 | 4个入口缺失，GL_OES_matrix_palette未声明 | 人物蒙皮兼容性仍是阻塞项 |
| 音频 | opened and tone queued，queue drained | 队列正常，扬声器实际声音需观察确认 |

缺失的4个图形入口：glCurrentPaletteMatrixOES、glLoadPaletteFromModelViewMatrixOES、glMatrixIndexPointerOES、glWeightPointerOES。需要实现实际兼容路径，不能用空函数冒充支持。

日志中的183帧是平台探测绘制次数，不是游戏帧率。

## 已落实到0.2.1源代码

- 版本号从构建配置统一传入程序与NACP，移除写死的启动日志版本。
- 启动日志明确记录平台版/CPU版构建模式。
- 开发数据与日志路径统一改为 deadspacemobile，错误信息显示完整预期库路径。
- B键CPU探测添加开始/结束标记，以区分未执行、执行出错和执行结束。
- 增加包内 RomFS 数据源选择：挂载成功且存在库时，从 romfs:/gamedata 读取，否则使用开发目录。当前没有将游戏资源打入测试NRO，不能据此宣称NSP整合交付已经完成。

现有0.2.0二进制不受这些源码修改影响。0.2.1编译检查通过，新的路径和RomFS选择还没有实机验证。下一项运行验证应覆盖正确的数据布局、B键执行核心、以及矩阵调色板兼容实现。
