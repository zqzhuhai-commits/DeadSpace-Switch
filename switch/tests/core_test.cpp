#include "preflight.hpp"
#include "PVRTDecompress.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
static void check(bool ok,const char* name) { if (!ok) { std::fprintf(stderr,"FAIL: %s\n",name); std::exit(1); } }
static void put16(std::vector<uint8_t>& b,size_t off,unsigned v) { b[off]=v; b[off+1]=v>>8; }
static void put32(std::vector<uint8_t>& b,size_t off,uint32_t v) { for (int i=0;i<4;++i) b[off+i]=v>>(i*8); }
int main() {
    check(ds::sha1({})=="da39a3ee5e6b4b0d3255bfef95601890afd80709","SHA1 empty");
    check(ds::sha1({'a','b','c'})=="a9993e364706816aba3e25717850c26c9cd0d89d","SHA1 abc");
    check(ds::sha1(std::vector<uint8_t>(1000000,'a'))=="34aa973cd4c4daa4f61eeb2bdbad27316534016f","SHA1 million a");
    std::vector<uint8_t> elf(116,0);
    elf[0]=0x7f;elf[1]='E';elf[2]='L';elf[3]='F';elf[4]=1;elf[5]=1;elf[6]=1;
    put16(elf,16,3);put16(elf,18,40);put32(elf,20,1);put32(elf,28,52);
    put16(elf,40,52);put16(elf,42,32);put16(elf,44,2);
    put32(elf,52,1);put32(elf,68,116);put32(elf,72,116);put32(elf,84,2);
    std::string error;
    check(ds::validate_elf32(elf,error),"valid ELF32 ARM envelope");
    auto bad=elf;bad[4]=2;check(!ds::validate_elf32(bad,error),"reject ELF64");
    bad=elf;put32(bad,28,0xfffffff0);check(!ds::validate_elf32(bad,error),"reject program table overflow");
    bad=elf;put32(bad,56,115);check(!ds::validate_elf32(bad,error),"reject truncated segment");
    bad=elf;put32(bad,72,1);check(!ds::validate_elf32(bad,error),"reject filesz above memsz");
    for (size_t n=0;n<elf.size();++n) {
        auto truncated=elf;truncated.resize(n);
        check(!ds::validate_elf32(truncated,error),"reject every truncated prefix");
    }
    const std::array<uint32_t,8> compressed={0,0xffffffff,0,0xffffffff,0,0xffffffff,0,0xffffffff};
    std::array<uint8_t,256> rgba{};
    pvr::PVRTDecompressPVRTC(compressed.data(),0,8,8,rgba.data());
    for (auto v:rgba) check(v==255,"known white PVRTC fixture");
    const auto missing=ds::preflight("/this-path-does-not-exist-deadspace");
    check(!missing.ready() && !missing.library_present,"missing donor fails closed");
    std::puts("PASS: SHA1 vectors, bounded ELF parsing, missing donor, upstream PVRTC decoder");
}
