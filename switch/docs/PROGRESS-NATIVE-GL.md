# 原生 GL 桥接进度

基线：`8b60820f4e4174c03eaaad262da9c07e36e5b7e4`。本轮加入真实 GL 子集，尚未生成可玩游戏或 NSP。

后续已加入实验性高清查找、KTX 完整 mip 上传和动态/错误/预算回退，见 [高清运行时进度](PROGRESS-HD-RUNTIME.md)。以下待办及验证结果记录本文件的原始基线。

## 本次接通

`GuestGl` 将 ARM32 寄存器/栈参数转换为有类型的主机调用。guest 指针只经过有界内存复制，不直接当作主机地址。仅绑定 VBO/EBO 时，将整数转换为 GL API 定义的缓冲区偏移。

- 客户端顶点、法线、颜色和最多四组纹理坐标：记录 guest 指针，绘制前按步长和实际使用范围复制；VBO/EBO 另检查边界。未知的索引缓冲区内容明确拒绝，不用零填充影子数据猜索引。
- 普通及索引绘制、buffer 创建/上传/子区域更新、纹理对象、固定功能矩阵和部分状态/光照参数。
- 纹理上传保留 null 数据分配，支持 RGB/RGBA/灰度及部分打包像素类型，按 pack/unpack 行对齐计算实际访问长度。
- 查询结果按确定的向量长度写回 guest；GL 字符串复制到 guest，未知查询形状明确阻塞。当前不宣传任何可选扩展，避免让游戏假定未完成的扩展路径可用。
- FBO/RBO 常用接口和状态查询；桌面参考上下文仅对签名一致的 OES 名称进行别名转换，Switch 路径使用真实 GLES1 名称。
- ATC RGB、显式透明度和插值透明度可先解码为 RGBA8，再调用真实 `glTexImage2D`，并恢复 unpack 对齐。高清包的 `0x87ee` 现在具备上传基础。

未实现的函数会抛出明确错误，不再在原生模式中统一返回成功。图形统计记录 guest 调用尝试、绘制提交及纹理上传次数；计数不等于有效游戏帧或视觉正确。

实现类型和入口依据：[Khronos GLES1 头文件](https://github.com/KhronosGroup/OpenGL-Registry/blob/main/api/GLES/gl.h)。

## 接入方式

`lifecycle_probe` 可接收 `GraphicsBackend`：提供当前上下文的函数解析器和逐帧呈现回调时，实际原始库 GL 导入进入 `GuestGl`。未提供时保留原来的模拟 GL 诊断，日志明确区分两者。

Switch 主程序源码新增 **R** 键：创建 SDL GLES1.1 上下文，运行原始库生命周期及最多 8 次 DrawFrame 尝试，然后记录结果和上下文错误。原来的 **L** 键仍为模拟 GL 对照。R 不是独立游戏启动器，完成 DrawFrame 也不等于画面或关卡已经正确。

CMake 已将桥接及 ATC 源文件纳入 CPU 静态库，Switch Makefile 已纳入上下文入口源文件。当前仅更新源码，**本轮没有 Switch 交叉编译、NRO 测试包或 NSP 产物**。

## 本轮实测

```sh
LIBGL_ALWAYS_SOFTWARE=1 ASAN_OPTIONS=detect_leaks=0 \
  python3 switch/tests/run_tests.py --native-gl
g++ -std=c++20 -Wall -Wextra -Werror -fsyntax-only \
  -Ithird_party/dynarmic/src -Iswitch/include -Iswitch/runtime \
  switch/runtime/lifecycle_probe.cpp switch/runtime/guest_gl.cpp \
  switch/runtime/host_main.cpp
git diff --check
```

全部通过。新增测试动态创建独立 EGL pbuffer，再通过本次桥接代码绘制及读取 framebuffer：

- Renderer：`llvmpipe (LLVM 20.1.2, 256 bits)`。
- Version：`4.5 (Compatibility Profile) Mesa 25.2.8-0ubuntu0.24.04.2`。
- 6 次合法绘制提交，验证客户端顶点数组及非零 first、VBO/EBO 偏移、RGB 行填充、null 纹理分配及子区域上传、ATC 蓝色/不透明样本、FBO 完整状态。
- 检查 viewport/矩阵查询输出保护字节，拒绝索引范围越界、未初始化 EBO、错误颜色数组形状、短 ATC 和未知矩阵调色板函数；末尾真实 `glGetError` 为 0。
- ASan/UBSan 开启，LeakSanitizer 因当前环境限制关闭。其他 JNI、内存和导入回归一并通过。

这是 **主机桌面 GL 兼容配置的真实软件渲染回读**，不是 Switch GPU 测试、GLES1 真机验证或原游戏关卡截图。本次测试使用自制三角形、四边形和小块纹理；未执行原游戏 ARM 指令。实际生命周期代码通过严格语法编译，尚未完整链接或在原版数据上运行；新增 SDL/Switch 上下文入口也未交叉编译。

## 仍未完成

1. GL 桥仍是子集：固定点接口、matrix palette、DrawTex、映射缓冲区、部分查询和压缩纹理路径等尚未完成。设备已知缺少的矩阵调色板四个入口仍需兼容实现。
2. ATC 上传支持不等于高清包已启用。尚需原纹理键查找、KTX mip 读取、实际 PSV 对照、替换生命周期及显存预算；当前不会自动加载 `texture_hd/`。
3. 其他压缩格式直接交给原生 GL，不保证主机或 Switch 支持；需要按实际能力补充转码。仅支持既定查询/像素/数组类型，其他情况明确阻塞。
4. 当前缺少匹配的 PSV 原始库和数据，尚无法验证原游戏在本轮桥接中的实际停止点。线程调度、真实游戏音频、输入、存档及最终独立 NSP 构建仍待完成。
