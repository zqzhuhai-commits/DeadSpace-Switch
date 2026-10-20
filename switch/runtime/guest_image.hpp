#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ds {
// Guest addresses remain 32-bit integers; never cast into host function pointers.
class GuestImage {
public:
    static constexpr uint32_t base=0x10000000;
    std::vector<uint8_t> memory;
    std::unordered_map<std::string,uint32_t> exports;
    unsigned relative_relocations=0;
    struct Import { std::string name; uint32_t size; uint8_t type; };
    std::vector<Import> imports;
    std::vector<uint32_t> constructors;
    bool load(const std::string& path,std::string& error);
    bool bind_imports(const std::function<uint32_t(const Import&)>& resolve,std::string& error);
    bool read(uint32_t address,void* out,size_t size) const;
    bool write(uint32_t address,const void* data,size_t size);
    bool executable(uint32_t address,size_t size) const;
private:
    struct Segment { uint32_t start,size,flags; };
    std::vector<Segment> segments;
    struct Relocation { uint32_t target,addend,kind,value; std::string import; };
    std::vector<Relocation> pending;
    bool bound=false;
};
}
