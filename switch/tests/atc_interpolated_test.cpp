#include "atc_decompress.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
static void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
int main() {
    try {
        std::array<uint8_t,16> block{};
        uint64_t selectors=0;
        for(unsigned i=0;i<16;++i)selectors|=uint64_t(i%8)<<(3*i);
        for(unsigned i=0;i<6;++i)block[2+i]=selectors>>(8*i);
        std::array<uint8_t,64> rgba{};
        block[0]=255;block[1]=0;
        require(atc::decode_rgba_interpolated(block.data(),block.size(),4,4,rgba.data()),"ATC interpolation decode rejected");
        const uint8_t eight[]={255,0,218,182,145,109,72,36};
        for(unsigned i=0;i<16;++i)require(rgba[4*i+3]==eight[i%8],"Eight-entry alpha interpolation");
        block[0]=0;block[1]=255;
        require(atc::decode_rgba_interpolated(block.data(),block.size(),4,4,rgba.data()),"ATC endpoint mode rejected");
        const uint8_t six[]={0,255,51,102,153,204,0,255};
        for(unsigned i=0;i<16;++i)require(rgba[4*i+3]==six[i%8],"Six-entry alpha / transparent / opaque endpoints");
        std::array<uint8_t,40> guarded;guarded.fill(0xa5);
        require(atc::decode_rgba_interpolated(block.data(),block.size(),3,2,guarded.data()+8),"Cropped ATC block rejected");
        for(unsigned i=0;i<8;++i)require(guarded[i]==0xa5 && guarded[32+i]==0xa5,"Cropped ATC decode overwrote neighbour");
        guarded.fill(0xa5);
        require(!atc::decode_rgba_interpolated(block.data(),15,4,4,guarded.data()+8),"Truncated ATC block accepted");
        for(auto value:guarded)require(value==0xa5,"Failed decode changed output");
        require(!atc::decode_rgba_interpolated(block.data(),16,0,4,rgba.data()),"Zero width accepted");
        std::cout<<"PASS: ATC interpolated alpha modes, cropped block guards and truncated input rejection\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
