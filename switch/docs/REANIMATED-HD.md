# Reanimated v1.3 高清材质核验

2026-10-20 实际下载 [v1.3 APK](https://github.com/GINESTR0/DeadSpaceReanimated/releases/tag/v1.3)，没有安装或运行 APK。全文件 SHA256 与 GitHub 发布资产记录一致：

`dbd94ee54fc3fefdddb101fac4eba1e2058a5db7cfcf30ac41ad96e88b285a3a`

## 实际提取结果

- 目录：APK 内 `assets/texture_hd/`。
- 222 个 KTX1 文件，340,222,792 字节（约 324.46 MiB），已单独提取。
- 116 个 ETC1_RGB8_OES、106 个 ATC_RGBA_INTERPOLATED_ALPHA_AMD。
- 所有文件的头、形状、各 mip 层字节数及文件结束位置均通过结构核验。
- 分辨率最高 4096×4096；不是普通 PNG 覆盖包。

APK 中原始资源在 `assets/game/assets/`。通过静态检查其纹理加载入口，确认文件名键由 FNV-1a64 计算：小端 32 位宽度、高度和 GL 格式，再接原材质第零级像素/压缩数据。类型字段和后续 mip 层不参与该键。

该算法已与 APK 自带 M3G Image2D 资源独立比对：**222/222 个高清文件全部找到精确哈希匹配**。218 个材质宽高各放大 2 倍，4 个宽高各放大 4 倍。逐文件 SHA256、格式、尺寸和对应资源路径见 `REANIMATED-HD-AUDIT.json`。

**映射范围仅为该 APK 自带原始资源。尚未与用户 PSV 数据逐对象比对，也没有在 Switch 上验证高清效果。**

## 已完成的 ATC 格式兼容验证

已在可移植 ATC 解码器中新增 `atc::decode_rgba_interpolated`，输出 RGBA8888。针对本包全部 106 张 ATC 材质、1,115 个 mip 层，与独立的 texture2ddecoder 1.0.6 比对（将其 BGRA 输出转换为 RGBA后比较），845,392,488 个输出字节全部一致。另通过 ASan/UBSan 检查透明度两种端点模式、非整块尺寸裁切和短输入拒绝。该函数尚未接入 Switch 游戏加载路径，也不代表已完成整包转码或真机验证。详见 `REANIMATED-HD-ATC-VERIFICATION.json`。

## 接入 Switch 所需工作

1. 在实际纹理上传路径计算匹配键或建立经 PSV 数据核验的对应表，再加载高清替代。
2. 读取 KTX1 各 mip 层，并根据 Switch 实测格式支持选择直接压缩上传或离线转换。ATC 需要准备解码/转码路径，不能仅凭 Android 上可显示就认定 Switch 可直接使用。
3. 保持原模型、UV 和逻辑尺寸，另行处理 HUD 图集、字形和动态纹理更新；材质倍率不等同于整个界面直接等比例放大。
4. 先做按需加载与显存验证。4096×4096 RGBA8 单张基础层就有 64 MiB，不能将所有压缩文件同时展开常驻。
5. 最终将匹配后的高清资源内置 RomFS/NSP，符合用户无需另复制游戏数据目录的交付要求。

APK 的 `assets/game/libDeadSpace32.so` SHA1 是 `64a3323cd3cc0c483a645b62af8508624163ba9b`，**不匹配当前支持库白名单**；`lib/arm64-v8a/libdsr.so` 则是 Android 自定义运行时。提取工具只导出高清 KTX 和清单，不替换这两个运行库，也不覆盖现有游戏资源。

## 可复现工具

Python 3.11+，无需安装 Android 工具或执行 APK：

```sh
python3 switch/tools/reanimated_hd.py /path/to/DeadSpaceReanimated-1.3.apk \
  --extract /path/to/new/texture_hd --map-originals \
  --report /path/to/hd-audit.json
python3 switch/tests/reanimated_hd_test.py
```

工具锁定该发布版 SHA256，拒绝错误包、异常 KTX、重复/危险文件名及已存在的目标目录。失败时清理暂存内容，不发布半成品。它完成提取和映射，**尚未接入 Switch 游戏运行时**。公开仓库仅保存工具与元数据，不包含材质文件。

格式名称核对：[Khronos ATC 扩展规范](https://github.com/KhronosGroup/OpenGL-Registry/blob/main/extensions/AMD/AMD_compressed_ATC_texture.txt)。
