#pragma once
#include <cstdint>
namespace ds {
// Loading consumes CPU on several guest workers inside one draw call. Allow
// bounded additional work only after actual asset reads; never reset forever.
class LoadingBudget {
    bool enabled;
    uint64_t last_bytes,granted=200000000;
public:
    static constexpr uint64_t initial=200000000,chunk=100000000,ceiling=1000000000;
    LoadingBudget(bool loading,uint64_t bytes):enabled(loading),last_bytes(bytes){}
    uint64_t extend(uint64_t bytes){
        if(!enabled||bytes<=last_bytes||granted>=ceiling)return 0;
        last_bytes=bytes;granted+=chunk;return chunk;
    }
    uint64_t total() const{return granted;}
};
}
