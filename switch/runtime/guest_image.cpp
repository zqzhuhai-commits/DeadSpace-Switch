#include "guest_image.hpp"
#include "preflight.hpp"
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace ds {
bool GuestImage::load(const std::string& path,std::string& error) {
    memory.clear();exports.clear();segments.clear();relative_relocations=0;
    try {
        std::ifstream f(path,std::ios::binary|std::ios::ate);
        const auto length=f.tellg();
        if (!f || length<52 || length>64*1024*1024) throw std::runtime_error("Invalid donor file size");
        std::vector<uint8_t> raw(static_cast<size_t>(length));f.seekg(0);
        if (!f.read(reinterpret_cast<char*>(raw.data()),raw.size())) throw std::runtime_error("Donor read failed");
        if (!validate_elf32(raw,error)) throw std::runtime_error(error);
        if (sha1(raw)!=donor_sha1) throw std::runtime_error("Unsupported donor SHA1");
        auto u16=[&](uint64_t p)->uint16_t { if(p+2>raw.size())throw std::runtime_error("Truncated ELF16");return raw[p]|uint16_t(raw[p+1])<<8; };
        auto u32=[&](uint64_t p)->uint32_t { if(p+4>raw.size())throw std::runtime_error("Truncated ELF32");return raw[p]|uint32_t(raw[p+1])<<8|uint32_t(raw[p+2])<<16|uint32_t(raw[p+3])<<24; };
        uint64_t end=0;
        for(unsigned i=0;i<u16(44);++i) {
            const uint64_t p=u32(28)+uint64_t(i)*32;
            if(u32(p)!=1)continue;
            const uint32_t va=u32(p+8),size=u32(p+20);
            end=std::max(end,uint64_t(va)+size);
            if(end>64*1024*1024)throw std::runtime_error("Guest image size bound exceeded");
            for(const auto& s:segments)if(va<uint64_t(s.start)+s.size && uint64_t(va)+size>s.start)throw std::runtime_error("Overlapping LOAD segments");
            segments.push_back({va,size,u32(p+24)});
        }
        memory.resize(end,0);
        for(unsigned i=0;i<u16(44);++i) {
            const uint64_t p=u32(28)+uint64_t(i)*32;
            if(u32(p)==1)std::memcpy(memory.data()+u32(p+8),raw.data()+u32(p+4),u32(p+16));
        }
        if(u16(46)!=40)throw std::runtime_error("Invalid section header size");
        auto section=[&](unsigned index)->uint64_t {if(index>=u16(48))throw std::runtime_error("Invalid section index");const uint64_t p=u32(32)+uint64_t(index)*40;if(p+40>raw.size())throw std::runtime_error("Truncated sections");return p;};
        for(unsigned i=0;i<u16(48);++i) {
            const auto s=section(i);const auto type=u32(s+4),off=u32(s+16),size=u32(s+20);
            if(type!=11 && type!=9)continue;
            if(uint64_t(off)+size>raw.size())throw std::runtime_error("Section outside file");
            if(type==11) {
                if(u32(s+36)!=16 || size%16)throw std::runtime_error("Invalid symbols");
                const auto st=section(u32(s+24));const uint32_t str_off=u32(st+16),str_size=u32(st+20);
                if(uint64_t(str_off)+str_size>raw.size())throw std::runtime_error("Invalid string table");
                for(uint64_t p=off;p<uint64_t(off)+size;p+=16) {
                    if(!u16(p+14))continue;
                    const auto n=u32(p);if(n>=str_size)throw std::runtime_error("Bad symbol name");
                    const char* name=reinterpret_cast<const char*>(raw.data()+str_off+n);
                    if(!std::memchr(name,0,str_size-n))throw std::runtime_error("Unterminated symbol name");
                    if(u16(p+14)!=0xfff1 && u32(p+4)>0xffffffffu-base)throw std::runtime_error("Export address overflow");
                    if(*name)exports[name]=u16(p+14)==0xfff1?u32(p+4):base+u32(p+4);
                }
            } else {
                if(u32(s+36)!=8 || size%8)throw std::runtime_error("Invalid relocations");
                for(uint64_t p=off;p<uint64_t(off)+size;p+=8) {
                    const uint32_t target=u32(p),info=u32(p+4);
                    if((info&255)!=23)continue; // externals intentionally unbound
                    if(info>>8)throw std::runtime_error("RELATIVE with symbol");
                    if(target>0xffffffffu-base)throw std::runtime_error("Relocation address overflow");
                    uint8_t v[4];if(!read(base+target,v,4))throw std::runtime_error("Relocation target outside image");
                    uint32_t addend=v[0]|uint32_t(v[1])<<8|uint32_t(v[2])<<16|uint32_t(v[3])<<24;
                    const uint32_t result=base+addend;
                    for(unsigned k=0;k<4;++k)memory[target+k]=result>>(8*k);
                    ++relative_relocations;
                }
            }
        }
        error.clear();return true;
    } catch(const std::exception& e) {
        error=e.what();memory.clear();segments.clear();exports.clear();return false;
    }
}
bool GuestImage::read(uint32_t address,void* out,size_t length) const {
    if(address<base)return false;
    const uint64_t p=address-base;
    for(const auto& s:segments)if(p>=s.start && p+length<=uint64_t(s.start)+s.size){std::memcpy(out,memory.data()+p,length);return true;}
    return false;
}
bool GuestImage::write(uint32_t address,const void* in,size_t length) {
    if(address<base)return false;
    const uint64_t p=address-base;
    for(const auto& s:segments)if((s.flags&2) && p>=s.start && p+length<=uint64_t(s.start)+s.size){std::memcpy(memory.data()+p,in,length);return true;}
    return false;
}
bool GuestImage::executable(uint32_t address,size_t length) const {
    if(address<base)return false;
    const uint64_t p=address-base;
    for(const auto& s:segments)if((s.flags&1) && p>=s.start && p+length<=uint64_t(s.start)+s.size)return true;
    return false;
}
}
