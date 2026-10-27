#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
namespace ds {
inline double guest_double(uint32_t lo,uint32_t hi){uint64_t bits=uint64_t(lo)|(uint64_t(hi)<<32);double value;std::memcpy(&value,&bits,8);return value;}
inline uint64_t guest_double_bits(double value){uint64_t bits;std::memcpy(&bits,&value,8);return bits;}
template<class Write> uint64_t guest_modf(uint32_t lo,uint32_t hi,uint32_t output,Write write){
    if(!output||output>0xfffffff8u)throw std::runtime_error("Invalid modf output pointer");
    double integral;const auto fraction=std::modf(guest_double(lo,hi),&integral);
    const auto bits=guest_double_bits(integral);write(output,uint32_t(bits));write(output+4,uint32_t(bits>>32));
    return guest_double_bits(fraction);
}
inline uint64_t guest_ldexp(uint32_t lo,uint32_t hi,uint32_t exponent){return guest_double_bits(std::ldexp(guest_double(lo,hi),int32_t(exponent)));}
}
