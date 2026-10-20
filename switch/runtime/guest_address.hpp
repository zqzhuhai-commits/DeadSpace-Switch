#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ds {
struct GuestLayout {
    static constexpr uint32_t image_base=0x10000000, image_limit=64*1024*1024;
    static constexpr uint32_t stack_base=0x20000000, stack_size=2*1024*1024;
    static constexpr uint32_t heap_base=0x30000000, heap_size=64*1024*1024;
};
static_assert(uint64_t(GuestLayout::image_base)+GuestLayout::image_limit<=GuestLayout::stack_base);
static_assert(uint64_t(GuestLayout::stack_base)+GuestLayout::stack_size<=GuestLayout::heap_base);
struct GuestSegment {uint32_t start,size,flags;};
// LOAD offsets have no load bias. Guest addresses acquire the bias exactly once.
inline std::optional<size_t> image_range(const std::vector<GuestSegment>& segments,size_t storage,uint32_t address,size_t length,uint32_t flags=0) {
    if(address<GuestLayout::image_base || storage>GuestLayout::image_limit || length>storage)return std::nullopt;
    const uint64_t offset=uint64_t(address)-GuestLayout::image_base;
    if(offset>storage || length>storage-offset)return std::nullopt;
    for(const auto& s:segments) {
        if((s.flags&flags)!=flags || uint64_t(s.start)+s.size>storage)continue;
        if(offset>=s.start && offset-s.start<=s.size && length<=s.size-(offset-s.start))return size_t(offset);
    }
    return std::nullopt;
}
}
