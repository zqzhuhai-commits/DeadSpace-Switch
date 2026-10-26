#pragma once
#include <cstdint>
#include <map>
#include <optional>
namespace ds {
// Reusable anonymous intervals, shared with a bounded POSIX-style sbrk area.
class GuestArena {
    uint32_t first,last,brk;
    std::map<uint32_t,uint32_t> regions;
public:
    GuestArena(uint32_t begin,uint32_t end):first(begin),last(end),brk(begin){}
    uint32_t program_break()const{return brk;}
    uint32_t lowest_mapping()const{return regions.empty()?last:regions.begin()->first;}
    std::optional<uint32_t> grow(int32_t amount){const int64_t next=int64_t(brk)+amount;if(next<first||next>lowest_mapping())return std::nullopt;const auto old=brk;brk=uint32_t(next);return old;}
    std::optional<uint32_t> map(uint32_t length){
        const uint64_t rounded=(uint64_t(length)+4095)&~uint64_t(4095);
        if(!rounded||rounded>last-first||regions.size()>=4096)return std::nullopt;
        const auto size=uint32_t(rounded);uint32_t top=last;
        for(auto it=regions.rbegin();it!=regions.rend();++it){if(uint64_t(it->first)+it->second+size<=top){const auto at=top-size;regions.emplace(at,size);return at;}top=it->first;}
        if(uint64_t(brk)+size>top)return std::nullopt;
        const auto at=top-size;regions.emplace(at,size);return at;
    }
    bool unmap(uint32_t start,uint32_t length){
        const uint64_t end=uint64_t(start)+((uint64_t(length)+4095)&~uint64_t(4095));
        if((start&4095)||!length||start<first||end>last)return false;
        for(auto it=regions.begin();it!=regions.end();){const uint32_t begin=it->first;const uint64_t old_end=uint64_t(begin)+it->second;
            if(old_end<=start||begin>=end){++it;continue;}it=regions.erase(it);
            if(begin<start)regions.emplace(begin,start-begin);
            if(old_end>end)regions.emplace(uint32_t(end),uint32_t(old_end-end));}
        return true;
    }
    bool contains(uint32_t address)const{if(address>=first&&address<brk)return true;auto it=regions.upper_bound(address);if(it==regions.begin())return false;--it;return uint64_t(address)-it->first<it->second;}
};
}
