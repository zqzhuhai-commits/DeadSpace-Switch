#include "preflight.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sys/stat.h>

namespace ds {
static uint32_t rol(uint32_t x, unsigned n) { return (x << n) | (x >> (32-n)); }
std::string sha1(const std::vector<uint8_t>& input) {
    auto b = input;
    const uint64_t bits = uint64_t(b.size()) * 8;
    b.push_back(0x80);
    while (b.size()%64 != 56) b.push_back(0);
    for (int i=7; i>=0; --i) b.push_back(uint8_t(bits >> (i*8)));
    uint32_t h[5] = {0x67452301,0xefcdab89,0x98badcfe,0x10325476,0xc3d2e1f0};
    for (size_t pos=0; pos<b.size(); pos+=64) {
        uint32_t w[80];
        for (unsigned i=0; i<16; ++i) {
            const auto* p = b.data()+pos+i*4;
            w[i]=(uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];
        }
        for (unsigned i=16; i<80; ++i) w[i]=rol(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
        uint32_t a=h[0],c=h[2],d=h[3],e=h[4],v=h[1];
        for (unsigned i=0; i<80; ++i) {
            uint32_t f,k;
            if (i<20) { f=(v&c)|(~v&d); k=0x5a827999; }
            else if (i<40) { f=v^c^d; k=0x6ed9eba1; }
            else if (i<60) { f=(v&c)|(v&d)|(c&d); k=0x8f1bbcdc; }
            else { f=v^c^d; k=0xca62c1d6; }
            const uint32_t t=rol(a,5)+f+e+k+w[i];
            e=d; d=c; c=rol(v,30); v=a; a=t;
        }
        h[0]+=a; h[1]+=v; h[2]+=c; h[3]+=d; h[4]+=e;
    }
    char out[41];
    for (int i=0; i<5; ++i) std::snprintf(out+i*8,9,"%08x",unsigned(h[i]));
    return out;
}
static uint16_t u16(const std::vector<uint8_t>& b, size_t p) {
    return uint16_t(b[p])|(uint16_t(b[p+1])<<8);
}
static uint32_t u32(const std::vector<uint8_t>& b, size_t p) {
    return uint32_t(b[p])|(uint32_t(b[p+1])<<8)|(uint32_t(b[p+2])<<16)|(uint32_t(b[p+3])<<24);
}
static bool range(size_t total, uint64_t off, uint64_t len) {
    return off<=total && len<=total-off;
}
bool validate_elf32(const std::vector<uint8_t>& b, std::string& error) {
    error.clear();
    auto fail=[&](const char* s){error=s;return false;};
    if (b.size()<52) return fail("ELF header truncated");
    if (std::memcmp(b.data(),"\x7f" "ELF",4)) return fail("Not an ELF library");
    if (b[4]!=1 || b[5]!=1 || b[6]!=1) return fail("Expected ELF32 little-endian v1");
    if (u16(b,16)!=3 || u16(b,18)!=40 || u32(b,20)!=1) return fail("Expected ARM ET_DYN");
    if (u16(b,40)!=52 || u16(b,42)!=32) return fail("Invalid ELF header sizes");
    const uint32_t phoff=u32(b,28);
    const uint16_t count=u16(b,44);
    if (!count || !range(b.size(),phoff,uint64_t(count)*32)) return fail("Invalid program header table");
    bool load=false,dynamic=false;
    for (unsigned i=0; i<count; ++i) {
        const size_t p=phoff+size_t(i)*32;
        const uint32_t type=u32(b,p),off=u32(b,p+4),va=u32(b,p+8);
        const uint32_t filesz=u32(b,p+16),memsz=u32(b,p+20),align=u32(b,p+28);
        if (!range(b.size(),off,filesz)) return fail("Segment extends beyond file");
        if (type==1) {
            if (filesz>memsz || uint64_t(va)+memsz>0x100000000ull) return fail("Invalid ARM32 load range");
            if (align>1 && ((align&(align-1)) || (va%align)!=(off%align))) return fail("Invalid load alignment");
            load=true;
        }
        if (type==2) dynamic=true;
    }
    if (!load || !dynamic) return fail("Missing LOAD or DYNAMIC segment");
    return true;
}
static bool regular(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(),&st)==0 && S_ISREG(st.st_mode) && st.st_size>0;
}
Report preflight(const std::string& root) {
    Report r;
    const auto path=root+"/lib/armeabi/libEAMGameDeadSpace.so";
    std::ifstream f(path,std::ios::binary|std::ios::ate);
    if (!f) { r.messages.push_back("Missing: lib/armeabi/libEAMGameDeadSpace.so"); return r; }
    r.library_present=true;
    const auto size=f.tellg();
    if (size<52 || size>64*1024*1024) { r.messages.push_back("Library outside expected size bounds"); return r; }
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(bytes.data()),bytes.size())) { r.messages.push_back("Library read failed"); return r; }
    std::string error;
    r.elf_valid=validate_elf32(bytes,error);
    r.messages.push_back(r.elf_valid?"ELF32 ARM structure: OK":error);
    r.sha1=sha1(bytes);
    r.donor_matches=r.sha1==donor_sha1;
    r.messages.push_back(r.donor_matches?"Xperia Play v1.1.33 SHA1: OK":"Unsupported library SHA1: "+r.sha1);
    r.assets_present=regular(root+"/assets/EAMCore.ini") && regular(root+"/assets/published/data/achievements.sb");
    r.messages.push_back(r.assets_present?"Asset sentinels: OK (not a full integrity check)":"Missing EAMCore.ini or published/data/achievements.sb");
    r.messages.push_back("Game lifecycle and external JNI services: NOT IMPLEMENTED");
    return r;
}
}
