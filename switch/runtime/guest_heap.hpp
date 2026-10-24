#pragma once
#include "guest_address.hpp"
#include <algorithm>
#include <map>
#include <iterator>
#include <stdexcept>

namespace ds {
class GuestHeap {
    using FreeIndex=std::multimap<uint32_t,uint32_t>;
    struct Block {uint32_t size,requested;bool available;FreeIndex::iterator free_slot{};};
    std::map<uint32_t,Block> blocks;
    FreeIndex available;
    uint32_t end=GuestLayout::heap_base;
public:
    GuestHeap()=default;
    GuestHeap(const GuestHeap&)=delete;
    GuestHeap& operator=(const GuestHeap&)=delete;
    uint32_t high_water() const {return end;}
    uint32_t allocation_size(uint32_t pointer) const {
        const auto it=blocks.find(pointer);
        if(it==blocks.end() || it->second.available)throw std::runtime_error("Invalid guest allocation");
        return it->second.requested;
    }
    uint32_t allocate(uint32_t requested) {
        if(requested>32*1024*1024)throw std::runtime_error("Guest allocation size bound");
        const uint32_t size=std::max(16u,(requested+15)&~15u);
        const auto slot=available.lower_bound(size);
        if(slot!=available.end()) {
            const auto pointer=slot->second;auto& block=blocks.at(pointer);
            const auto remaining=block.size-size;available.erase(slot);
            if(remaining){auto added=blocks.emplace(pointer+size,Block{remaining,0,true}).first;added->second.free_slot=available.emplace(remaining,pointer+size);}
            block={size,requested,false};return pointer;
        }
        if(size>GuestLayout::heap_base+GuestLayout::heap_size-end)throw std::runtime_error("Guest heap exhausted");
        const auto pointer=end;blocks.emplace(pointer,Block{size,requested,false});end+=size;return pointer;
    }
    template<class Zero> uint32_t zeroed(uint32_t count,uint32_t size,Zero zero) {
        const uint64_t total=uint64_t(count)*size;
        if(total>32*1024*1024)throw std::runtime_error("Guest calloc size bound");
        const auto pointer=allocate(uint32_t(total));
        try {zero(pointer,uint32_t(total));}
        catch(...){release(pointer);throw;}
        return pointer;
    }
    template<class Copy> uint32_t reallocate(uint32_t pointer,uint32_t size,Copy copy) {
        if(!pointer)return allocate(size);
        const auto old_size=allocation_size(pointer);
        if(!size){release(pointer);return 0;}
        const auto replacement=allocate(size);
        try {copy(replacement,pointer,std::min(old_size,size));}
        catch(...){release(replacement);throw;}
        release(pointer);return replacement;
    }
    void release(uint32_t pointer) {
        if(!pointer)return;
        auto it=blocks.find(pointer);
        if(it==blocks.end() || it->second.available)throw std::runtime_error("Invalid guest free");
        it->second.available=true;it->second.requested=0;
        auto after=std::next(it);
        if(after!=blocks.end() && after->second.available){available.erase(after->second.free_slot);it->second.size+=after->second.size;blocks.erase(after);}
        if(it!=blocks.begin()) {
            auto before=std::prev(it);
            if(before->second.available){available.erase(before->second.free_slot);before->second.size+=it->second.size;blocks.erase(it);it=before;}
        }
        it->second.free_slot=available.emplace(it->second.size,it->first);
    }
};
}
