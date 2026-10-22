#pragma once
#include <array>
#include <cstdint>
#include <sys/stat.h>

namespace ds {
// Bionic ARM stat64 is 104 bytes. Never copy the host struct into guest RAM.
inline std::array<uint8_t,104> guest_stat(const struct stat& st) {
    std::array<uint8_t,104> bytes{};
    const auto put=[&](unsigned offset,uint64_t value,unsigned size){
        for(unsigned i=0;i<size;++i)bytes[offset+i]=uint8_t(value>>(8*i));
    };
    put(0,st.st_dev,8);put(12,st.st_ino,4);put(16,st.st_mode,4);
    put(20,st.st_nlink,4);put(24,st.st_uid,4);put(28,st.st_gid,4);
    put(32,st.st_rdev,8);put(48,uint64_t(st.st_size),8);
    put(56,st.st_blksize,4);put(64,st.st_blocks,8);
    put(72,st.st_atim.tv_sec,4);put(76,st.st_atim.tv_nsec,4);
    put(80,st.st_mtim.tv_sec,4);put(84,st.st_mtim.tv_nsec,4);
    put(88,st.st_ctim.tv_sec,4);put(92,st.st_ctim.tv_nsec,4);
    put(96,st.st_ino,8);
    return bytes;
}
}
