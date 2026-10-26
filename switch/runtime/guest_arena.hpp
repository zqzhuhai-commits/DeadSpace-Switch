#pragma once
#include <cstdint>
#include <map>
#include <optional>

namespace ds {
// Shared address space for the guest allocator's sbrk and anonymous mmap.
// Unmapped holes are reusable; no host pointer is exposed to the guest.
class GuestArena {
    uint32_t bottom=0x40000000u,limit=0x50000000u,brk=bottom;
    std::map<uint32_t,uint32_t> mappings;
public:
    uint32_t program_break()const{return brk;}
    uint32_t lowest_mapping()const{return mappings.empty()?limit:mappings.begin()->first;}
    uint64_t mapped_bytes()const{uint64_t n=0;for(const auto& kv:mappings)n+=kv.second;return n;}
    std::optional<uint32_t> grow(int32_t increment){
        const int64_t end=int64_t(brk)+increment;
        if(end<bottom||end>lowest_mapping())return std::nullopt;
        const auto old=brk;brk=uint32_t(end);return old;
    }
    std::optional<uint32_t> map(uint32_t length){
        const uint64_t size=(uint64_t(length)+4095)&~uint64_t(4095);
        if(!length||size>limit-bottom)return std::nullopt;
        uint32_t high=limit;
        for(auto it=mappings.rbegin();it!=mappings.rend();++it){
            const uint64_t end=uint64_t(it->first)+it->second;
            if(uint64_t(high)>=end+size){const auto p=high-uint32_t(size);mappings.emplace(p,uint32_t(size));return p;}
            high=it->first;
        }
        const uint64_t low=(uint64_t(brk)+4095)&~uint64_t(4095);
        if(uint64_t(high)<low+size)return std::nullopt;
        const auto p=high-uint32_t(size);mappings.emplace(p,uint32_t(size));return p;
    }
    bool unmap(uint32_t address,uint32_t length){
        const uint64_t size=(uint64_t(length)+4095)&~uint64_t(4095),end=uint64_t(address)+size;
        if((address&4095)||!length||address<bottom||end>limit||address<brk)return false;
        // Linux permits holes; split surviving prefixes/suffixes as needed.
        auto it=mappings.begin();
        while(it!=mappings.end()){
            const uint32_t begin=it->first;const uint64_t finish=uint64_t(begin)+it->second;
            if(finish<=address){++it;continue;}if(begin>=end)break;
            it=mappings.erase(it);
            if(begin<address)mappings.emplace(begin,address-begin);
            if(finish>end)mappings.emplace(uint32_t(end),uint32_t(finish-end));
        }
        return true;
    }
    bool contains(uint32_t address)const{
        if(address>=bottom&&address<brk)return true;
        auto it=mappings.upper_bound(address);if(it==mappings.begin())return false;--it;
        return uint64_t(address)-it->first<it->second;
    }
};
}
