#pragma once
#include <cstddef>
#include <cstdint>
using Result=uint32_t;
#define R_FAILED(r) ((r)!=0)
enum JitType {JitType_CodeMemory,JitType_SetProcessMemoryPermission};
struct Jit {JitType type;void* rw_addr;void* rx_addr;size_t size;bool is_executable;};
Result jitCreate(Jit*,size_t);
Result jitClose(Jit*);
void* jitGetRwAddr(Jit*);
void* jitGetRxAddr(Jit*);
Result jitTransitionToExecutable(Jit*);
Result jitTransitionToWritable(Jit*);
void armDCacheFlush(const void*,size_t);
void armICacheInvalidate(const void*,size_t);
