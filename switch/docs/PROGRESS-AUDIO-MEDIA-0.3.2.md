# 音频、原生图形、高清资源与基础输入接入

开发基线：`842a326d309edd40471559ed683b4ddf04e4b4a2`。

接入 AudioTrack 的 signed little-endian PCM/SDL 输出、队列背压和线程重试，修复匿名 mmap/munmap 区间复用，补充 guest qsort、GLES 16.16 固定点入口与 1280×720 横屏的自然屏幕方向转换。新增基础触摸/数字按键以及主机脚本输入；双摇杆、体感替代、完整存档和持续运行前端仍待完成。

本次恢复了聊天中已经实现、但在临时目录重置前尚未提交的改动。恢复后必须重新构建和验证；最终测试记录将在本文件后续补充。此前主机观察为：25,000 次 DrawFrame 返回，难度选择画面，11 张高清替换、114 层 mip、非零 PCM。此前观察不等于本次代码已重测，也不等于 Switch 实机通过。

主机媒体探测器需要 SDL2 开发头文件、运行库及 EGL/桌面兼容 GL。可选 CMake 参数：`-DDS_BUILD_MEDIA_PROBE=ON`。运行：

```sh
SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 host-build/ds-media-probe \
  gamedata/lib/armeabi/libEAMGameDeadSpace.so gamedata 30000 texture_hd \
  frame.ppm touch.tsv
```

`dummy` 只用于无扬声器的主机联调，不能作为实际听音证据。触摸脚本每行是 `frame down|move|up x y`，坐标使用 1280×720 输出表面。

高清目标保留 Reanimated v1.3 的 222 张 KTX；当前原版数据只精确匹配其中 54 个键。不能把有限场景中的命中数量当作全部材质已兼容。游戏资源和材质不提交 Git。

未生成新的 NRO/NSP；尚未完成 Switch 交叉编译、实机、第一关可操作及存取档验收。
