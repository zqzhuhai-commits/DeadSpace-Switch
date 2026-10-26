#pragma once
#include <algorithm>
#include <cstdint>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace ds {
// Comparators receive actual guest element addresses, never host pointers.
// Keep the array in place until all comparisons have returned.
template<class Read,class Write,class Compare>
void guest_qsort(uint32_t base,uint32_t count,uint32_t size,Read read,Write write,Compare compare){
    if(!count||!size)return;
    const uint64_t bytes=uint64_t(count)*size;
    if(count>65536||bytes>64*1024*1024||uint64_t(base)+bytes>uint64_t(UINT32_MAX)+1)throw std::runtime_error("Guest qsort range/size limit");
    std::vector<uint8_t> original(static_cast<size_t>(bytes));
    for(uint32_t i=0;i<bytes;++i)original[i]=read(base+i);
    std::vector<uint32_t> order(count);std::iota(order.begin(),order.end(),0u);
    uint32_t calls=0;
    std::stable_sort(order.begin(),order.end(),[&](uint32_t a,uint32_t b){if(++calls>2*1024*1024)throw std::runtime_error("Guest qsort comparator limit");return compare(base+a*size,base+b*size)<0;});
    for(uint32_t i=0;i<count;++i)for(uint32_t j=0;j<size;++j)write(base+i*size+j,original[order[i]*size+j]);
}
}
