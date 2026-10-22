#include "guest_transfer.hpp"
#include <array>
#include <cstring>
#include <map>
#include <stdexcept>
#include <vector>
void check(bool ok){if(!ok)throw std::runtime_error("Guest transfer regression");}
struct Memory {
    static constexpr uint32_t base=0x30000000,image=0x10000000;
    std::map<uint32_t,std::array<uint8_t,4096>> pages;
    std::array<uint8_t,4096> ro{};
    size_t fast=0,slow=0;
    bool span_read(uint32_t a,void* out,size_t n){
        if(a>=base&&uint64_t(a)+n<=base+3*4096&&(a>>12)==((a+n-1)>>12)){
            auto it=pages.find((a-base)/4096);if(it==pages.end())std::memset(out,0,n);else std::memcpy(out,it->second.data()+(a&4095),n);++fast;return true;
        }
        if(a>=image&&uint64_t(a)+n<=image+ro.size()){std::memcpy(out,ro.data()+a-image,n);++fast;return true;}
        return false;
    }
    bool span_write(uint32_t a,const void* in,size_t n){
        if(a>=base&&uint64_t(a)+n<=base+3*4096&&(a>>12)==((a+n-1)>>12)){
            std::memcpy(pages[(a-base)/4096].data()+(a&4095),in,n);++fast;return true;
        }
        return false;
    }
    void read(uint32_t a,void* p,size_t n){ds::read_guest_bytes(a,p,n,[&](uint32_t x,void* y,size_t z){return span_read(x,y,z);},[&](uint32_t x){++slow;uint8_t v;if(!span_read(x,&v,1))throw std::runtime_error("unmapped read");return v;});}
    void write(uint32_t a,const void* p,size_t n){ds::write_guest_bytes(a,p,n,[&](uint32_t x,const void* y,size_t z){return span_write(x,y,z);},[&](uint32_t x,uint8_t v){++slow;if(!span_write(x,&v,1))throw std::runtime_error("unmapped/readonly write");});}
};
template<class F> void rejects(F action){bool threw=false;try{action();}catch(const std::runtime_error&){threw=true;}check(threw);}
int main(){
    Memory memory;std::vector<uint8_t> expected(3*4096),actual(expected.size());
    for(size_t i=0;i<4096;++i){expected[i]=uint8_t(i*31);memory.pages[0][i]=expected[i];}
    memory.read(Memory::base,actual.data(),actual.size());
    check(actual==expected&&memory.pages.size()==1&&memory.fast==3&&memory.slow==0);
    // Cover many unaligned transfers, including exact ends and page crossings.
    for(uint32_t start: {0u,1u,4093u,4095u,4096u,8189u,12287u}){
        const size_t count=std::min<size_t>(73,expected.size()-start);
        std::vector<uint8_t> input(count);for(size_t i=0;i<count;++i)input[i]=uint8_t(i+start);
        memory.write(Memory::base+start,input.data(),count);
        std::copy(input.begin(),input.end(),expected.begin()+start);
        memory.read(Memory::base,actual.data(),actual.size());check(actual==expected);
    }
    // Snapshot semantics used by memcpy/memmove and JNI array regions.
    std::vector<uint8_t> overlap(64);memory.read(Memory::base+4,overlap.data(),overlap.size());memory.write(Memory::base+7,overlap.data(),overlap.size());
    std::memmove(expected.data()+7,expected.data()+4,64);
    memory.read(Memory::base,actual.data(),actual.size());check(actual==expected&&memory.slow==0);
    const auto before=memory.ro;uint8_t value=9;
    rejects([&]{memory.write(Memory::image,&value,1);});check(memory.ro==before);
    rejects([&]{memory.read(Memory::image+4090,actual.data(),16);});
    rejects([&]{memory.read(0x70000000,actual.data(),1);});
    const auto calls=memory.fast;
    rejects([&]{memory.write(0xfffffff0,actual.data(),32);});
    rejects([&]{memory.read(0,actual.data(),64*1024*1024+1);});
    check(memory.fast==calls);
    memory.read(0,nullptr,0);memory.write(0,nullptr,0);check(memory.fast==calls);
    // The last representable byte is valid when a backend explicitly maps it.
    uint8_t end=0;
    ds::read_guest_bytes(0xffffffff,&end,1,[](uint32_t,void* out,size_t){*static_cast<uint8_t*>(out)=77;return true;},[](uint32_t){return uint8_t(0);});
    check(end==77);
}
