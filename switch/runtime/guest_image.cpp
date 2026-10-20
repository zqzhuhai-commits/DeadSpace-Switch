#include "guest_image.hpp"
#include "preflight.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace ds {
bool GuestImage::load(const std::string& path,std::string& error) {
    memory.clear();exports.clear();segments.clear();imports.clear();constructors.clear();pending.clear();bound=false;relative_relocations=0;
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
            if(end>GuestLayout::image_limit)throw std::runtime_error("Guest image size bound exceeded");
            for(const auto& s:segments)if(va<uint64_t(s.start)+s.size && uint64_t(va)+size>s.start)throw std::runtime_error("Overlapping LOAD segments");
            segments.push_back({va,size,u32(p+24)});
        }
        memory.resize(end,0);
        for(unsigned i=0;i<u16(44);++i) {
            const uint64_t p=u32(28)+uint64_t(i)*32;
            if(u32(p)==1)std::memcpy(memory.data()+u32(p+8),raw.data()+u32(p+4),u32(p+16));
        }
        // memory stores ELF-relative bytes; read/write add the load bias once.
        // A second bias would overlap the runtime's 0x20000000 stack region.
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
                    const auto n=u32(p);if(n>=str_size)throw std::runtime_error("Bad symbol name");
                    const char* name=reinterpret_cast<const char*>(raw.data()+str_off+n);
                    if(!std::memchr(name,0,str_size-n))throw std::runtime_error("Unterminated symbol name");
                    if(!u16(p+14)) { if(*name)imports.push_back({name,u32(p+8),uint8_t(raw[p+12]&15)});continue; }
                    if(u16(p+14)!=0xfff1 && u32(p+4)>0xffffffffu-base)throw std::runtime_error("Export address overflow");
                    if(*name)exports[name]=u16(p+14)==0xfff1?u32(p+4):base+u32(p+4);
                }
            } else {
                if(u32(s+36)!=8 || size%8)throw std::runtime_error("Invalid relocations");
                for(uint64_t p=off;p<uint64_t(off)+size;p+=8) {
                    const uint32_t target=u32(p),info=u32(p+4);
                    if((info&255)!=23) {
                        const auto kind=info&255;
                        if(kind==0)continue;
                        if(kind!=2 && kind!=3 && kind!=21 && kind!=22)throw std::runtime_error("Unsupported relocation kind");
                        const auto syms=section(u32(s+24));
                        const auto symbol=uint64_t(u32(syms+16))+uint64_t(info>>8)*16;
                        if(u32(syms+36)!=16 || uint64_t(info>>8)*16+16>u32(syms+20))throw std::runtime_error("Relocation symbol outside table");
                        const auto strings=section(u32(syms+24));const auto n=u32(symbol);
                        if(n>=u32(strings+20) || uint64_t(u32(strings+16))+u32(strings+20)>raw.size())throw std::runtime_error("Bad relocation symbol string");
                        const char* name=reinterpret_cast<const char*>(raw.data()+u32(strings+16)+n);
                        if(!std::memchr(name,0,u32(strings+20)-n))throw std::runtime_error("Unterminated relocation name");
                        uint8_t bytes[4];
                        if(target>0xffffffffu-base || !read(base+target,bytes,4))throw std::runtime_error("Relocation target outside image");
                        const auto addend=uint32_t(bytes[0])|uint32_t(bytes[1])<<8|uint32_t(bytes[2])<<16|uint32_t(bytes[3])<<24;
                        const auto defined=u16(symbol+14);
                        pending.push_back({target,addend,kind,defined?(defined==0xfff1?u32(symbol+4):base+u32(symbol+4)):0,defined?"":name});
                        continue;
                    }
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
        // Old Android linker marks .init_array as PROGBITS; dynamic tags are authoritative.
        uint32_t init=0,init_size=0;
        for(unsigned i=0;i<u16(48);++i) {
            const auto s=section(i);
            if(u32(s+4)!=6)continue;
            const auto off=u32(s+16),size=u32(s+20);
            if(size%8 || uint64_t(off)+size>raw.size())throw std::runtime_error("Invalid dynamic section");
            for(uint64_t p=off;p<uint64_t(off)+size;p+=8) {
                if(u32(p)==25)init=u32(p+4);
                if(u32(p)==27)init_size=u32(p+4);
            }
        }
        if(init_size%4 || init_size>65536)throw std::runtime_error("Invalid initializer array size");
        for(uint32_t n=0;n<init_size;n+=4) {
            uint8_t b[4];if(!read(base+init+n,b,4))throw std::runtime_error("Initializer array outside image");
            const uint32_t fn=b[0]|uint32_t(b[1])<<8|uint32_t(b[2])<<16|uint32_t(b[3])<<24;
            if(fn && fn!=0xffffffffu)constructors.push_back(fn);
        }
        error.clear();return true;
    } catch(const std::exception& e) {
        error=e.what();memory.clear();segments.clear();exports.clear();imports.clear();constructors.clear();pending.clear();return false;
    }
}
bool GuestImage::bind_imports(const std::function<uint32_t(const Import&)>& resolve,std::string& error) {
    try {
        if(bound)throw std::runtime_error("Imports already bound");
        std::unordered_map<std::string,uint32_t> addresses;
        for(const auto& item:imports) {
            const auto address=resolve(item);
            if(!address)throw std::runtime_error("Unresolved import: "+item.name);
            addresses[item.name]=address;
        }
        std::vector<std::pair<uint32_t,uint32_t>> writes;
        for(const auto& r:pending) {
            const uint32_t s=r.import.empty()?r.value:addresses.at(r.import);
            const uint32_t value=r.kind==2?s+r.addend:r.kind==3?s+r.addend-(base+r.target):s;
            writes.emplace_back(r.target,value);
        }
        // Only the loader writes text relocations; guest writes retain segment protection.
        for(const auto& w:writes)for(unsigned k=0;k<4;++k)memory[w.first+k]=w.second>>(8*k);
        bound=true;error.clear();return true;
    } catch(const std::exception& e) {error=e.what();return false;}
}
bool GuestImage::read(uint32_t address,void* out,size_t length) const {
    const auto offset=image_range(segments,memory.size(),address,length);
    if(!offset)return false;
    if(length)std::memcpy(out,memory.data()+*offset,length);
    return true;
}
bool GuestImage::write(uint32_t address,const void* in,size_t length) {
    const auto offset=image_range(segments,memory.size(),address,length,2);
    if(!offset)return false;
    if(length)std::memcpy(memory.data()+*offset,in,length);
    return true;
}
bool GuestImage::executable(uint32_t address,size_t length) const {
    return image_range(segments,memory.size(),address,length,1).has_value();
}
}
