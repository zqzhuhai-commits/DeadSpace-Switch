# 0.3.2 后续：JNI 与真实资源读取

基线：`5d400bc60e1a51cc623cc1ac8d6a84b7f3d91997`。本次修复运行层代码，不是新的可玩版本；没有生成 NSP。

## 修改及效果

- 从生命周期执行器抽出 `GuestJni`，由实际 Dynarmic 执行器继承。测试直接运行这个类的同一份实现，不另写一套假 JNI。
- 修复数组和字符串操作将 `JNIEnv` 当作对象的问题；方法委托使用传入的实例接收者，资源流各自保留位置和关闭状态。
- 共享函数表注册器覆盖 32 个连续的原始数组槽位及 60 个实例/静态方法槽位，保留普通、V、A 后缀。旧代码每隔 8 个槽注册一次，且将三种调用全标成普通调用。
- 根据方法描述符解析原始数组、对象数组和对象参数。普通/V 调用按 ARM32 规则处理 64 位对齐及浮点可变参数提升，A 调用按每个 jvalue 8 字节读取。jlong/jdouble 返回值在实际执行器中写入 r0:r1。
- 支持 UTF-16 和 JNI modified UTF-8，包括 NUL 与代理项；字符长度、编码字节数分别计算，isCopy 只写一个字节。
- 资源 open/openFd 读取真实文件；list 返回实际目录的直接子项，排序后生成字符串数组。read 处理偏移、零长度和 EOF，skip 对负数返回 0。越界、找不到文件和已关闭流产生可查询、可清除的 Java 异常，不再静默夹取长度或永远报告无异常。
- `NewObject` 的三种调用会建立真正的实例记录并调用构造函数委托。执行器仅接受已核对的六类无状态 EA 委托的无参构造，其他构造仍明确阻塞。
- 不再靠猜测间接指针补救错误的 JNI 解码，也不再拿缓存 AssetManager 代替 Startup 的真实参数。
- 生命周期成功报告增加实际打开流数、列目录次数与送达 guest 的资源字节数。这些计数是诊断证据，不代表关卡完成。

实现依据：[JNI 函数规范](https://docs.oracle.com/javase/8/docs/technotes/guides/jni/spec/functions.html)、仓库 `jni/jni.h` 的函数表及参数声明、仓库六个 EA 委托构造函数。ARM32 可变参数按 [AAPCS32](https://github.com/ARM-software/abi-aa/blob/main/aapcs32/aapcs32.rst) 的基础调用规则实现。

## 本轮验证

```sh
ASAN_OPTIONS=detect_leaks=0 python3 switch/tests/run_tests.py
g++ -std=c++20 -Wall -Wextra -Werror -fsyntax-only \
  -Ithird_party/dynarmic/src -Iswitch/include switch/runtime/lifecycle_probe.cpp
python3 switch/tests/hd_romfs_test.py
git diff --check
```

全部通过。JNI 测试以 ASan/UBSan 和 `-Wall -Wextra -Werror` 编译，覆盖函数表注册、普通/V/A 参数、64 位返回、实例构造、UTF 编码、数组边界、独立文件流、真实目录列举及异常。LeakSanitizer 因当前环境的 `/proc` 限制关闭，地址和未定义行为检测仍启用。

已取回固定版本的 Dynarmic 子模块，对实际生命周期执行器做语法编译，验证继承、虚函数签名和集成可编译。**语法编译不等于完整链接或 Switch 交叉编译。** CMake 已纳入新源文件和主机 `ds-jni-test` 目标，本轮测试采用直接 g++ 编译运行，尚未执行 CMake 构建。

## 仍然未完成

1. 当前没有受支持的 PSV 原始库及数据，尚不能用真实游戏重跑 JNI 生命周期；上述资源读取验证使用磁盘上的测试文件，不是第一关或高清材质运行证据。
2. GL 仍为诊断模拟接口；线程调度、真实音频、游戏输入和存档尚未接通。0.3.2 映像镜像、嵌套 guest 栈和 guest 分配器仍需继续审查。
3. 本 JNI 子集尚不实现线程绑定的 JNIEnv、局部引用帧回收、完整 Java 类系统、Java monitor、所有构造及全部 JNI API。诊断对象和字符串副本存活到运行时销毁，不适合直接当作无限期游戏运行时。
4. 高清包的提取、PSV 匹配和 RomFS 准备功能保持；纹理替换仍未接入实际 GL 路径。最终仍以全资源内置、自包含 NSP 为交付目标。
