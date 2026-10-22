#pragma once
#include <cstdint>
#include <stdexcept>
#include <vector>
namespace ds {
// Comparators receive guest element addresses, never host pointers.
template<class Read,class Write,class Compare>
void guest_sort(uint32_t base,uint32_t count,uint32_t size,Read read,Write write,Compare compare){
    if(count<2)return;
    const uint64_t bytes=uint64_t(count)*size;
    if(!base||!size||bytes>64*1024*1024||uint64_t(base)+bytes>0x100000000ull)throw std::runtime_error("qsort range bound");
    std::vector<uint8_t> temp(size);uint64_t comparisons=0;
    auto less=[&](uint32_t a,uint32_t b){if(++comparisons>8000000)throw std::runtime_error("qsort comparison budget");return compare(base+a*size,base+b*size)<0;};
    auto swap=[&](uint32_t a,uint32_t b){a=base+a*size;b=base+b*size;for(uint32_t i=0;i<size;++i)temp[i]=read(a+i);for(uint32_t i=0;i<size;++i)write(a+i,read(b+i));for(uint32_t i=0;i<size;++i)write(b+i,temp[i]);};
    auto sift=[&](uint32_t root,uint32_t length){while(uint64_t(root)*2+1<length){uint32_t child=root*2+1;if(child+1<length&&less(child,child+1))++child;if(!less(root,child))return;swap(root,child);root=child;}};
    for(uint32_t i=count/2;i>0;--i)sift(i-1,count);
    for(uint32_t end=count-1;end>0;--end){swap(0,end);sift(0,end);}
}
}
