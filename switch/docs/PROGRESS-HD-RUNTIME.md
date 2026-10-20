# 高清纹理运行时接入

基线：`c80040a1c1a34425c6073aa809ff3af8726ca1a8`。新增实验性高清替换入口，并通过主机原生 GL 回读；没有验证原游戏或 Switch 画面，也没有生成可玩 NSP。

## 上传路径

原生 GL 模式收到第零级纹理上传后，使用审计确认的算法计算键：`FNV-1a64(LE32 width,height,GL format || level0 bytes)`。普通像素上传先剔除 unpack 行填充；压缩上传使用原始压缩字节计算键，之后才解码。没有按名字或尺寸猜对应关系。

匹配 `texture_hd/<16hex>.ktx` 后，验证 KTX1 头、单面二维形状、编码、完整 mip 链及每层长度；要求材质宽高按相同整数倍率增大，并检查真实 GL 最大纹理尺寸。边界为单文件 32 MiB、最大 4096×4096、metadata 1 MiB。

高清纹理作为独立主机 GL 对象上传完整 mip 链，只在绘制时临时绑定。每次从原对象复制实际的过滤和 wrap 参数，绘制后恢复原绑定及活动单元。原始对象继续接收游戏上传，因此后续原版 mip 不会覆盖高清 mip；相同基础图再次上传也不会重复建立高清对象。

RGB、RGBA、alpha、luminance 等纹理保留原来的通道语义。不会将 RGB 原纹理简单换成 RGBA 而改变 GL_REPLACE 下的片元透明度。

## 解码与回退

- ETC1、ATC RGB/显式透明度/插值透明度、PVRTC1 RGB/RGBA 可解码后上传，减少对驱动压缩格式支持的依赖。ATC 使用已核验解码器；ETC/PVRTC 使用仓库已有 PowerVR 解码器，并在外层补充长度、尺寸、输入对齐和块裁切检查。
- 高清对象以 RGBA8 全 mip 大小保守计费，默认预算 **128 MiB**，保留原对象的成本另计。超出预算或驱动尺寸能力则使用原图，不自动扩大预算。
- 缺失、损坏、倍率错误及原生上传失败均保留原图并记录统计。当前缺失/错误键在本次运行中缓存，修改资源后需重启重新加载。
- 纹理收到 `glTexSubImage2D` 时，删除高清对象并继续更新原对象；该对象到删除为止不再做高清替换，适用于字体图集等动态纹理。原始图像和 mip 均保留在 GPU，无需猜测或重建原图来回退。
- 纹理作为 framebuffer 目标附件时也禁用高清替换，避免后续采样到过期的静态高清内容。
- 原始 GL 错误被高清探测遇到时会排队交还 `glGetError`，不以可选资源检查吞掉原调用错误。

Switch R 键诊断入口源码已配置可选加载：包内游戏数据对应 `romfs:/texture_hd/`，开发模式对应 `sdmc:/switch/deadspacemobile/texture_hd/`。目录不存在时自动使用原材质。最终内置布局仍由 `prepare_romfs.py --hd-apk` 准备，不要求最终 NSP 用户复制额外数据目录。

## 验证结果

```sh
LIBGL_ALWAYS_SOFTWARE=1 ASAN_OPTIONS=detect_leaks=0 \
  python3 switch/tests/run_tests.py --native-gl
python3 switch/tests/hd_pack_test.py /path/to/texture_hd \
  --reference-site-packages /path/to/texture2ddecoder-1.0.6 \
  --report /path/to/decode-verification.json
python3 switch/tests/hd_romfs_test.py
```

主机 ASan/UBSan、JNI/内存/导入及高清 RomFS 回归通过；实际生命周期、GL 桥和加载器的严格语法编译通过。泄漏检测因环境限制关闭，地址及未定义行为检测开启。

Mesa/llvmpipe 4.5 compatibility 的真实像素回读验证：高清蓝图替换原红图、完整 mip 链、原 mip 后续上传不覆盖高清、相同基础图复用、绘制后原对象宽度/绑定保持、动态更新切回绿色原图、损坏文件回退、RGB 行填充哈希及 GL_REPLACE 透明度、显存预算不足回退。测试使用自制数据，未执行原游戏 ARM 指令。

此外，对已提取的 Reanimated v1.3 **全部 222 张纹理、2,310 个 mip 层**，逐文件核对已提交审计中的 SHA256，再将本次运行时 C++ 输出与独立 `texture2ddecoder 1.0.6` 对比：

| 编码 | 纹理 | mip 层 |
| --- | ---: | ---: |
| ETC1 | 116 | 1,195 |
| ATC 插值透明度 | 106 | 1,115 |

**1,876,164,400 个 RGBA 输出字节全部一致。** 报告为 `HD-RUNTIME-DECODE-VERIFICATION.json`。此包对比以优化主机 C++ 库运行，未对全部资源执行 ASan；合成边界测试和原生绘制回归使用 ASan/UBSan。PVRTC 仅完成白色块及最小 mip 测试，未对完整原游戏材质做独立逐字节核验。

## 仍需完成的工作

1. 当前未找到可用于联调的原始 PSV 数据包。高清 APK 的游戏库不匹配白名单，未替换或放宽验证。必须用受支持的库和实际资源检查哪些键会命中、启动实际停止点及后续线程行为。
2. `runtime_integrated` 的既有数据准备清单仍保留 false，表示尚未验证真实游戏整合；本轮完成的是实验代码接线和主机测试。
3. GL 固定点、matrix palette 等接口、线程调度、真实音频、输入和存档仍不完整。高清替换不能解决这些运行阻塞项。
4. 新增 R 键上下文入口尚未交叉编译；本轮未生成 NRO/NSP、未做 Switch GPU 或安装验证。完成第一关、存取档和无外置数据运行验收前，不能标记完整版。
