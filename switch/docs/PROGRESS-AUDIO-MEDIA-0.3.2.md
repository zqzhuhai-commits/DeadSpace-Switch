# 音频、原生图形、高清资源与基础输入接入

开发基线：`842a326d309edd40471559ed683b4ddf04e4b4a2`。

接入 AudioTrack 的 signed little-endian PCM/SDL 输出、队列背压和线程重试，修复匿名 mmap/munmap 区间复用，补充 guest qsort、GLES 16.16 固定点入口与 1280×720 横屏的自然屏幕方向转换。新增基础触摸/数字按键以及主机脚本输入；双摇杆、体感替代、完整存档和持续运行前端仍待完成。

恢复后的代码已重新构建。ASan/UBSan 完整测试及 Mesa 原生 GL 像素回读通过，包含 PCM JNI 的单位/偏移/背压、匿名映射复用/局部释放、排序载荷和输入 softfp 参数检查。

实际 R36S donor + Reanimated v1.3 主机复测：第 20,000 帧触摸右下角确认（1145,675），20,003 帧抬起。程序离开难度界面，在第 20,631 帧调用尚未实现的 `tanf` 后停止。此时 74,500 次绘制、519 次纹理上传，11 张高清命中/114 层 mip；PCM 3,622,912 字节，1,775,960 个非零样本。停止时截图为黑屏，不构成第一关可操作证据。已按现有 ARM softfp 数学分发补齐 `tanf`/`tan`，后续复测另记。

Switch AArch64 交叉编译通过（devkitA64 25.1 / GCC 13.2.0）；仅说明编译链接成功。R 键仍为 8 帧诊断入口，不能用这个入口体验完整游戏。

主机媒体探测器需要 SDL2 开发头文件、运行库及 EGL/桌面兼容 GL。可选 CMake 参数：`-DDS_BUILD_MEDIA_PROBE=ON`。运行：

```sh
SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 host-build/ds-media-probe \
  gamedata/lib/armeabi/libEAMGameDeadSpace.so gamedata 30000 texture_hd \
  frame.ppm touch.tsv
```

`dummy` 只用于无扬声器的主机联调，不能作为实际听音证据。触摸脚本每行是 `frame down|move|up x y`，坐标使用 1280×720 输出表面。

高清目标保留 Reanimated v1.3 的 222 张 KTX；当前原版数据只精确匹配其中 54 个键。不能把有限场景中的命中数量当作全部材质已兼容。游戏资源和材质不提交 Git。

已生成开发诊断 NRO；未生成 NSP，尚未完成实机、第一关可操作及存取档验收。
