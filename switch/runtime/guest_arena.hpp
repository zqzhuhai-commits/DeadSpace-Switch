#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace ds {
class GuestArena {
    static constexpr uint32_t begin=0x40000000,end=0x50000000;
    uint32_t brk_=begin;
    std::map<uint32_t,uint32_t> mappings;
public:
    uint32_t program_break()const{return brk_;}
    uint32_t lowest_mapping()const{return mappings.empty()?end:mappings.begin()->first;}
    std::optional<uint32_t> change_break(int32_t increment){
        const int64_t next=int64_t(brk_)+increment;
        if(next<begin||next>lowest_mapping())return std::nullopt;
        const auto previous=brk_;brk_=uint32_t(next);return previous;
    }
    bool contains(uint32_t p)const{
        if(p>=begin&&p<brk_)return true;
        auto it=mappings.upper_bound(p);if(it==mappings.begin())return false;--it;
        return uint64_t(p)-it->first<it->second;
    }
    std::optional<uint32_t> map(uint32_t requested){
        const uint64_t size=(uint64_t(requested)+4095)&~uint64_t(4095);
        const uint64_t floor=(uint64_t(brk_)+4095)&~uint64_t(4095);
        if(!size||size>end-floor)return std::nullopt;
        uint64_t ceiling=end;
        for(auto it=mappings.rbegin();it!=mappings.rend();++it){
            if(ceiling>=uint64_t(it->first)+it->second+size){const auto address=uint32_t(ceiling-size);mappings.emplace(address,uint32_t(size));return address;}
            ceiling=it->first;
        }
        if(ceiling<floor+size)return std::nullopt;
        const auto address=uint32_t(ceiling-size);mappings.emplace(address,uint32_t(size));return address;
    }
    // Unmapping holes succeeds, as on Linux. Split partial live mappings and
    // report only actual released pages so the memory owner can discard them.
    bool unmap(uint32_t address,uint32_t length,std::vector<std::pair<uint32_t,uint32_t>>& released){
        released.clear();const uint64_t size=(uint64_t(length)+4095)&~uint64_t(4095),stop=uint64_t(address)+size;
        if(!size||(address&4095)||address<((uint64_t(brk_)+4095)&~uint64_t(4095))||stop>end)return false;
        std::vector<std::pair<uint32_t,uint32_t>> keep;
        for(auto it=mappings.begin();it!=mappings.end();){
            const uint64_t lo=it->first,hi=lo+it->second;
            if(lo>=stop||hi<=address){++it;continue;}
            const auto left=std::max<uint64_t>(lo,address),right=std::min<uint64_t>(hi,stop);
            released.emplace_back(uint32_t(left),uint32_t(right-left));
            if(lo<address)keep.emplace_back(uint32_t(lo),uint32_t(address-lo));
            if(hi>stop)keep.emplace_back(uint32_t(stop),uint32_t(hi-stop));
            it=mappings.erase(it);
        }
        for(const auto& part:keep)mappings.emplace(part);
        return true;
    }
};
}
