#include "hd_texture.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F> void reject(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}require(failed,"Invalid HD input accepted");}
static void word(std::vector<uint8_t>& out,uint32_t value){for(unsigned i=0;i<4;++i)out.push_back(uint8_t(value>>(8*i)));}
static void put(std::vector<uint8_t>& out,size_t offset,uint32_t value){for(unsigned i=0;i<4;++i)out.at(offset+i)=uint8_t(value>>(8*i));}
static std::vector<uint8_t> ktx(uint32_t w,uint32_t h) {
    std::vector<uint8_t> out={0xab,0x4b,0x54,0x58,0x20,0x31,0x31,0xbb,0x0d,0x0a,0x1a,0x0a};
    uint32_t levels=1;for(auto n=std::max(w,h);n>1;n>>=1)++levels;
    for(uint32_t v:{0x04030201u,0u,1u,0u,0x8d64u,0x1908u,w,h,0u,0u,1u,levels,0u})word(out,v);
    for(uint32_t level=0;level<levels;++level){const auto x=std::max(1u,w>>level),y=std::max(1u,h>>level),size=((x+3)/4)*((y+3)/4)*8;word(out,size);out.resize(out.size()+size,0);}
    return out;
}
int main(){
    try {
        std::vector<uint8_t> pixels;for(unsigned i=0;i<16;++i)pixels.push_back(uint8_t(i));
        require(ds::texture_key(2,2,0x1908,pixels)=="8b955b6c3c9b6d9a","FNV key differs from independent native vector");
        const auto input=ktx(6,5);auto texture=ds::HdTexture::parse(input);require(texture.levels.size()==3,"Wrong full mip count");
        for(unsigned i=0;i<texture.levels.size();++i){const auto rgba=texture.rgba(i);require(rgba.size()==size_t(texture.levels[i].width)*texture.levels[i].height*4,"Wrong cropped ETC output size");for(size_t p=0;p<rgba.size();p+=4)require(rgba[p]==2&&rgba[p+1]==2&&rgba[p+2]==2&&rgba[p+3]==255,"ETC1 known block mismatch");}
        reject([&]{auto bad=input;bad.pop_back();ds::HdTexture::parse(bad);});
        reject([&]{auto bad=input;bad.push_back(0);ds::HdTexture::parse(bad);});
        reject([&]{auto bad=input;put(bad,56,1);ds::HdTexture::parse(bad);});
        reject([&]{auto bad=input;put(bad,36,8192);ds::HdTexture::parse(bad);});
        reject([&]{auto bad=input;put(bad,64,0xffffffff);ds::HdTexture::parse(bad);});
        reject([&]{auto bad=input;put(bad,60,3);ds::HdTexture::parse(bad);});
        reject([&]{auto bad=input;put(bad,52,6);ds::HdTexture::parse(bad);});
        reject([&]{ds::read_hd_texture("/nonexistent","../escape");});
        require(ds::read_hd_texture("/nonexistent","0123456789abcdef").empty(),"Missing texture not distinguishable");
        char pattern[]="/tmp/deadspacemobile-hd-XXXXXX";char* directory=mkdtemp(pattern);require(directory!=nullptr,"Temporary directory failed");
        struct Cleanup {std::filesystem::path path;~Cleanup(){std::filesystem::remove_all(path);}} cleanup{directory};
        const auto filename=std::string(directory)+"/0123456789abcdef.ktx";
        {std::ofstream file(filename,std::ios::binary);file.write(reinterpret_cast<const char*>(input.data()),input.size());}
        require(ds::read_hd_texture(directory,"0123456789abcdef")==input,"Native file loader changed KTX bytes");
        {std::ofstream file(filename,std::ios::binary|std::ios::trunc);file<<"bad";}
        reject([&]{ds::read_hd_texture(directory,"0123456789abcdef");});
        const std::vector<uint8_t> atc={255,0,0,0,0,0,0,0,31,0,0,0,0,0,0,0};
        const auto blue=ds::decode_texture_rgba(0x87ee,1,1,atc);require(blue==std::vector<uint8_t>({0,0,255,255}),"Cropped ATC block mismatch");
        std::vector<uint8_t> pvrtc;for(unsigned i=0;i<4;++i){word(pvrtc,0);word(pvrtc,0xffffffff);}
        const auto white=ds::decode_texture_rgba(0x8c02,8,8,pvrtc);for(auto b:white)require(b==255,"PVRTC fixture mismatch");
        require(ds::decode_texture_rgba(0x8c02,1,1,pvrtc)==std::vector<uint8_t>({255,255,255,255}),"PVRTC small mip mismatch");
        reject([&]{ds::decode_texture_rgba(0x8c02,6,4,pvrtc);});
        reject([&]{ds::decode_texture_rgba(0x8c02,8,8,std::vector<uint8_t>(31));});
        reject([&]{ds::decode_texture_rgba(0x8d64,4,4,std::vector<uint8_t>(7));});
        std::cout<<"PASS: HD FNV key, bounded KTX/mips, cropped ETC1/ATC, PVRTC regular/small mips and missing file fallback\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
