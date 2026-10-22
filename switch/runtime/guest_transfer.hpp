#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
namespace ds {
inline void transfer_range(uint32_t address,size_t count) {
    if(count>64*1024*1024||uint64_t(address)+count>0x100000000ull)
        throw std::runtime_error("Guest transfer range/size limit");
}
template<class Span,class Byte> void read_guest_bytes(uint32_t address,void* output,size_t count,Span span,Byte byte) {
    transfer_range(address,count);
    auto* out=static_cast<uint8_t*>(output);
    while(count){
        const auto n=std::min(count,size_t(4096-(address&4095)));
        if(!span(address,out,n))for(size_t i=0;i<n;++i)out[i]=byte(address+uint32_t(i));
        address+=uint32_t(n);out+=n;count-=n;
    }
}
template<class Span,class Byte> void write_guest_bytes(uint32_t address,const void* input,size_t count,Span span,Byte byte) {
    transfer_range(address,count);
    const auto* in=static_cast<const uint8_t*>(input);
    while(count){
        const auto n=std::min(count,size_t(4096-(address&4095)));
        if(!span(address,in,n))for(size_t i=0;i<n;++i)byte(address+uint32_t(i),in[i]);
        address+=uint32_t(n);in+=n;count-=n;
    }
}
}
