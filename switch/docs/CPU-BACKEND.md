# ARM32 execution prototype — 0.3.0

Pinned dependency: [lioncash/dynarmic](https://github.com/lioncash/dynarmic), commit `a41c380246d3d9f9874f0f792d234dc0cc17c180`, 0BSD; bundled dependency licenses are retained in its submodule.

## What now works

The same bounded C++ ELF32 guest image and Dynarmic callbacks compile for Linux/x86-64 and Switch/AArch64. The real donor is SHA1 checked, LOAD segments are copied into a logical guest image, BSS is zeroed, and 22,677 relative relocations are applied. A separate bounded single-thread C++ constructor runtime binds all imports, applies symbol relocations, and executes all 209 initializers with 112 external calls. Its heap, mutex attributes/ownership, semaphores, atomics, allocator arena, clock and destructor registrations carry actual state. Unsupported services halt. JNI/game lifecycle is not initialized by this C++ runtime.

Linux host execution passed five genuine exports from the supplied game library, with returns 12, 393228, 262156, 0 and 524300. These are touch event identifiers, not gameplay. Additional fixtures verified Thumb-to-ARM return, VFP single-precision addition and unmapped-execute rejection. See `CPU-HOST-PROBE.txt` for observed output.

The Switch AArch64 Dynarmic backend and the 0.3.0 NRO compiled and linked successfully. **They have not been run on Switch.** B in this NRO attempts the CPU probes and constructor run with the user's donor at `/switch/deadspacemobile/gamedata/lib/armeabi/libEAMGameDeadSpace.so`.

## Switch JIT adaptation

`switch/patches/dynarmic-switch-jit.patch` adds libnx JIT allocation, distinct writable/executable aliases, permission transitions and libnx cache synchronization to Oaknut. Dynarmic's AArch64 code emitters write through the writable alias and calculate branches against the executable alias. Host platforms retain their original code-memory path.

Fastmem is disabled; every guest memory access goes through checked callbacks. The Generic CMake target uses Dynarmic's generic exception handler, not Linux signal handlers. FMT_OS is disabled on Horizon because its optional POSIX file wrapper is unnecessary here. No success stub substitutes for an unavailable JIT facility; allocation/transition errors are reported as failures.

## Build and reproduce

```sh
git clone --recurse-submodules https://github.com/zqzhuhai-commits/DeadSpace-Switch.git
cd DeadSpace-Switch
export DEVKITPRO=/opt/devkitpro
export BOOST_ROOT=/path/to/boost-headers-prefix
bash switch/tools/build_cpu_probe.sh
```

Requires CMake, Ninja, devkitA64/libnx, Switch SDL2/Mesa/nouveau and Boost headers. Tested Boost 1.83; no host Boost libraries are linked into the Switch binary. For an Ubuntu host, the headers can come from libboost-dev with `BOOST_ROOT=/usr`.

Host build:

```sh
cmake -S switch/runtime -B switch/build/host -G Ninja \
  -DBoost_NO_BOOST_CMAKE=ON -DBOOST_ROOT=/usr \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build switch/build/host --parallel 2
switch/build/host/ds-cpu-probe /path/to/libEAMGameDeadSpace.so
```

The host probe needs the original binary locally; it is never included in this public repository. The default `make -C switch` still builds the smaller platform-only probe. The CPU build uses `CPU_PROBE=1`; force a rebuild when switching configurations.

## Remaining before a title screen

Extend the constructor import dispatcher to Android/JNI/GL services; implement ARM32 arguments, 32-bit pointers and return handling; initialize JNI/JavaVM; call the actual lifecycle after constructors in the verified order; connect renderer/audio/input/storage. Legacy VFP short-vector mixer behavior needs dedicated testing beyond one scalar add. A five-function result is not evidence of a working game or a playable NSP.
