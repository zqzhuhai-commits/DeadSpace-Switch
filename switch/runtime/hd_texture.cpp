#include "hd_texture.hpp"
#include "../../src/atc_decompress.h"
#include "../../third_party/powervr/PVRTDecompress.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace ds {
namespace {
uint32_t word(const std::vector<uint8_t>& bytes,size_t p) {
    if(p>bytes.size() || bytes.size()-p<4)throw std::runtime_error("Truncated HD KTX word");
    return bytes[p]|uint32_t(bytes[p+1])<<8|uint32_t(bytes[p+2])<<16|uint32_t(bytes[p+3])<<24;
}
void dimensions(uint32_t w,uint32_t h) {if(!w||!h||w>4096||h>4096)throw std::runtime_error("Texture dimensions bound");}
}
HdTexture HdTexture::parse(std::vector<uint8_t> input) {
    static const uint8_t magic[]={0xab,0x4b,0x54,0x58,0x20,0x31,0x31,0xbb,0x0d,0x0a,0x1a,0x0a};
    if(input.size()<64 || input.size()>32*1024*1024 || std::memcmp(input.data(),magic,12))throw std::runtime_error("Invalid HD KTX magic/size");
    if(word(input,12)!=0x04030201 || word(input,16) || word(input,20)!=1 || word(input,24) || word(input,32)!=0x1908)throw std::runtime_error("Unsupported HD KTX encoding");
    HdTexture out;out.format=word(input,28);out.width=word(input,36);out.height=word(input,40);
    dimensions(out.width,out.height);
    if((out.format!=0x8d64&&out.format!=0x87ee)||word(input,44)||word(input,48)||word(input,52)!=1)throw std::runtime_error("Unsupported HD KTX shape/format");
    uint32_t count=1;for(uint32_t size=std::max(out.width,out.height);size>1;size>>=1)++count;
    const auto metadata=word(input,60);
    if(word(input,56)!=count || metadata>1024*1024 || metadata%4)throw std::runtime_error("Invalid HD KTX mip chain/metadata");
    size_t cursor=64+metadata;
    for(uint32_t level=0;level<count;++level) {
        const auto w=std::max(1u,out.width>>level),h=std::max(1u,out.height>>level);
        const uint32_t expected=((w+3)/4)*((h+3)/4)*(out.format==0x8d64?8:16);
        const auto size=word(input,cursor);cursor+=4;
        if(size!=expected || cursor>input.size() || size>input.size()-cursor)throw std::runtime_error("HD KTX mip length mismatch");
        out.levels.push_back({w,h,uint32_t(cursor),size});cursor+=(size+3)&~size_t(3);
    }
    if(cursor!=input.size())throw std::runtime_error("HD KTX trailing data");
    out.bytes=std::move(input);return out;
}
std::vector<uint8_t> HdTexture::rgba(unsigned index) const {
    const auto& level=levels.at(index);
    return decode_texture_rgba(format,level.width,level.height,std::vector<uint8_t>(bytes.begin()+level.offset,bytes.begin()+level.offset+level.size));
}
std::vector<uint8_t> decode_texture_rgba(uint32_t format,uint32_t w,uint32_t h,const std::vector<uint8_t>& data) {
    dimensions(w,h);
    std::vector<uint8_t> out(size_t(w)*h*4);
    if(format==0x8d64) {
        const uint32_t padded_w=(w+3)&~3u,padded_h=(h+3)&~3u;
        if(data.size()!=uint64_t(padded_w)*padded_h/2)throw std::runtime_error("ETC1 input size mismatch");
        // The vendored decoder assumes whole blocks and aligned input words.
        std::vector<uint32_t> aligned(data.size()/4);std::memcpy(aligned.data(),data.data(),data.size());
        std::vector<uint8_t> padded(size_t(padded_w)*padded_h*4);
        pvr::PVRTDecompressETC(aligned.data(),padded_w,padded_h,padded.data(),0);
        for(uint32_t y=0;y<h;++y)std::memcpy(out.data()+size_t(y)*w*4,padded.data()+size_t(y)*padded_w*4,w*4);
    } else if(format==0x87ee||format==0x8c93||format==0x8c92) {
        const uint64_t expected=uint64_t((w+3)/4)*((h+3)/4)*(format==0x8c92?8:16);
        if(data.size()!=expected)throw std::runtime_error("ATC input size mismatch");
        const bool ok=format==0x87ee?atc::decode_rgba_interpolated(data.data(),data.size(),int(w),int(h),out.data()):format==0x8c93?atc::decode_rgba_explicit(data.data(),data.size(),int(w),int(h),out.data()):atc::decode_rgb(data.data(),data.size(),int(w),int(h),out.data());
        if(!ok)throw std::runtime_error("Invalid ATC data");
    } else if(format>=0x8c00&&format<=0x8c03) {
        if((w&(w-1))||(h&(h-1)))throw std::runtime_error("PVRTC dimensions must be powers of two");
        const bool two=format==0x8c01||format==0x8c03;
        const uint64_t expected=uint64_t(std::max(w,two?16u:8u))*std::max(h,8u)*(two?2:4)/8;
        if(data.size()!=expected)throw std::runtime_error("PVRTC input size mismatch");
        std::vector<uint32_t> aligned(data.size()/4);std::memcpy(aligned.data(),data.data(),data.size());
        pvr::PVRTDecompressPVRTC(aligned.data(),two,w,h,out.data());
        if(format==0x8c00||format==0x8c01)for(size_t i=3;i<out.size();i+=4)out[i]=255;
    } else throw std::runtime_error("Unsupported texture decompression format");
    return out;
}
std::string texture_key(uint32_t w,uint32_t h,uint32_t format,const std::vector<uint8_t>& pixels) {
    uint64_t hash=0xcbf29ce484222325ull;
    const auto add=[&](uint8_t b){hash=(hash^b)*0x100000001b3ull;};
    for(uint32_t v:{w,h,format})for(unsigned shift=0;shift<32;shift+=8)add(uint8_t(v>>shift));
    for(auto b:pixels)add(b);
    char key[17];std::snprintf(key,sizeof(key),"%016llx",static_cast<unsigned long long>(hash));return key;
}
std::vector<uint8_t> read_hd_texture(const std::string& root,const std::string& key) {
    if(key.size()!=16 || key.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::runtime_error("Invalid HD texture key");
    std::ifstream file(root+"/"+key+".ktx",std::ios::binary|std::ios::ate);
    if(!file)return {};
    const auto length=file.tellg();if(length<64 || length>32*1024*1024)throw std::runtime_error("HD texture file size bound");
    std::vector<uint8_t> bytes(static_cast<size_t>(length));file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw std::runtime_error("HD texture read failed");
    return bytes;
}
}
