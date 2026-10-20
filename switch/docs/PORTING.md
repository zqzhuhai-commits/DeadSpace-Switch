# Runtime boundaries — 2026-10-20

The Switch target does not link upstream `src/main.cpp`, `loader/`, JNI or libc thunks yet. The upstream tree is retained as the source base; only its MIT PowerVR decoder is linked into the diagnostic NRO today.

| Boundary | Upstream | Switch work remaining |
| --- | --- | --- |
| CPU | Native ARM32 process | Demonstrate AArch32 support or ARM32-to-ARM64 execution. Standard libnx is AArch64. |
| ELF | mmap/mprotect and ARM trampolines in loader/ | Horizon memory permissions, cache synchronization and matching relocation strategy |
| ABI | Native pointers, softfp/hardfp thunks | 32-bit guest pointers, callbacks, structs, floats, TLS and exceptions if host is ARM64 |
| JNI | Java callbacks from native frame loop | Guest-aware JNI environment and actual guest callback execution |
| Graphics | 190 GLES1 fixed-function imports | Actual GLES1 context/translation, matrix palette and fixed-point behavior |
| Textures | PowerVR PVRTC decoder | Decoder fixture passes on host; real assets/GPU await hardware |
| Audio | AudioTrack to SDL plus legacy VFP patches | Actual guest mixer execution, buffers and audible output |
| Input | Xperia Play JNI touch/key/sensor callbacks | Bind game callbacks to Joy-Con, touch, menu cursor and gestures |
| Saves | Linux filesystem/libc bridges | Switch writable paths, checkpoint/reload, suspend/resume |

## Execution routes under investigation

The public xerpi/libnx AArch32 branch is a research candidate, not a validated SDK stack. Alternatively an ARM32-to-ARM64 execution core needs a complete guest/host ABI bridge. Reanimated describes this kind of mechanism but does not publish its core in the supplied repository.

Existing `loader/so_util_arm64.cpp` loads ARM64 guest code; it does not translate an ARM32 guest. Changing ELF typedefs, using Linux memory calls unchanged or casting ARM32 addresses to host pointers will not solve this.

The game actually requires GLES1.1. The upstream egl_shim.cpp introductory reference to GLES2 is not sufficient evidence of game graphics compatibility; src/main.cpp and thunks/khronos/gles1.cpp document the fixed-function requirements. An ES2-only triangle is only a host GPU smoke test.

## Required acceptance evidence

Record commit, donor hash, console/firmware, entry mode, log and observed behavior. Host qemu success is upstream Linux evidence only. First playable gate: actual title/new game, first-level movement/aim/fire, textures, game sound, pause, checkpoint save/reload and level transition. Full-campaign/stability testing comes later.
