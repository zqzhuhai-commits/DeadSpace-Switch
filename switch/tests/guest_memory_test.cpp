#include "guest_address.hpp"
#include "guest_call_scope.hpp"
#include "guest_heap.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F> static void reject(F fn){bool failed=false;try{fn();}catch(const std::exception&){failed=true;}check(failed,"Invalid guest operation accepted");}
struct Cpu {
    std::array<uint32_t,16> regs{};
    std::array<uint32_t,64> ext{};
    uint32_t cpsr=0x30,fpscr=0x1234;
    auto& Regs(){return regs;}auto& ExtRegs(){return ext;}
    uint32_t Cpsr(){return cpsr;}uint32_t Fpscr(){return fpscr;}
    void SetCpsr(uint32_t v){cpsr=v;}void SetFpscr(uint32_t v){fpscr=v;}
};
int main() {
    try {
        using L=ds::GuestLayout;
        const std::vector<ds::GuestSegment> segments={{0,256,5},{512,256,6}};
        check(ds::image_range(segments,768,L::image_base,4,1)==0,"Image base not applied exactly once");
        check(ds::image_range(segments,768,L::image_base+512,256,2)==512,"Writable image span rejected");
        check(!ds::image_range(segments,768,L::image_base,4,2),"Wrote read-only code");
        check(!ds::image_range(segments,768,L::image_base+512,4,1),"Executed data segment");
        check(!ds::image_range(segments,768,L::image_base+240,32),"Cross-segment span accepted");
        check(!ds::image_range(segments,768,L::image_base+256,4),"Unmapped image gap accepted");
        check(!ds::image_range(segments,768,L::stack_base,4),"Double-biased image overlaps stack");
        check(!ds::image_range(segments,768,0xfffffffcu,8),"Wrapped image address accepted");
        check(!ds::image_range(segments,768,L::image_base,SIZE_MAX),"Wrapped image length accepted");
        check(!ds::image_range({{L::image_base,256,7}},size_t(L::image_base)+768,L::stack_base,4),"Accepted oversized/double-biased storage");
        const auto top=ds::guest_call_sp(0,6,true);
        const auto parent=top-1024;
        const auto nested=ds::guest_call_sp(parent,12,false);
        check(!(top&7) && !(nested&7) && nested+32<=parent-256,"Nested stack arguments overwrite parent");
        check(ds::guest_call_sp(nested,4,false)<nested,"Recursive call did not move downward");
        reject([&]{ds::guest_call_sp(L::stack_base+32,6,false);});
        reject([&]{ds::guest_call_sp(L::stack_base-8,4,false);});
        reject([&]{ds::guest_call_sp(parent+1,4,false);});
        reject([&]{ds::guest_call_sp(parent,65,false);});
        Cpu cpu;
        for(unsigned i=0;i<cpu.regs.size();++i)cpu.regs[i]=0x11110000+i;
        for(unsigned i=0;i<cpu.ext.size();++i)cpu.ext[i]=0x22220000+i;
        const auto original=cpu;
        bool returned=true;uint32_t pending=17;uint64_t budget=123456;std::string fault="parent";
        {
            ds::GuestCallScope outer(cpu,returned,pending,budget,fault);
            cpu.regs.fill(31);cpu.ext.fill(41);cpu.cpsr=0x10;cpu.fpscr=2;
            returned=false;pending=18;budget=234;fault="outer";
            try {
                ds::GuestCallScope inner(cpu,returned,pending,budget,fault);
                cpu.regs.fill(99);cpu.ext.fill(88);cpu.cpsr=0;cpu.fpscr=0;
                returned=true;pending=0;budget=0;fault="failure";
                throw std::runtime_error("host service failed");
            } catch(const std::runtime_error&) {}
            check(cpu.regs[0]==31 && cpu.regs[13]==31 && cpu.ext[63]==41 && cpu.cpsr==0x10 && cpu.fpscr==2,"Exception damaged suspended CPU state");
            check(!returned && pending==18 && budget==234 && fault=="outer","Exception damaged dispatcher state");
        }
        check(cpu.regs==original.regs && cpu.ext==original.ext && cpu.cpsr==original.cpsr && cpu.fpscr==original.fpscr,"Nested scope failed to restore full architecture");
        check(returned && pending==17 && budget==123456 && fault=="parent","Outer scope failed to restore dispatcher");
        {
            ds::GuestCallScope top_scope(cpu,returned,pending,budget,fault);
            cpu.regs[0]=23;returned=false;pending=0;budget=0;fault.clear();top_scope.keep_top_level_result();
        }
        check(cpu.regs[0]==23 && returned && pending==17 && budget==123456 && fault=="parent","Successful top-level result semantics changed");
        ds::GuestHeap heap;
        auto a=heap.allocate(3),b=heap.allocate(17),c=heap.allocate(8);
        check(a==L::heap_base && !(a&15) && b==a+16 && c==b+32,"Guest heap alignment incorrect");
        heap.release(b);check(heap.allocate(16)==b,"Freed block not reused");
        heap.release(b);heap.release(a);check(heap.allocate(40)==a,"Adjacent free blocks not coalesced");
        reject([&]{heap.release(a+1);});
        heap.release(a);reject([&]{heap.release(a);});
        heap.release(c);
        const auto water=heap.high_water();
        for(unsigned i=0;i<10000;++i){auto p=heap.allocate(32);heap.release(p);}
        check(heap.high_water()==water,"Repeated allocate/free exhausts guest heap");
        std::vector<uint8_t> bytes(1024);
        auto get=[&](uint32_t address)->uint8_t&{return bytes.at(address-L::heap_base);};
        a=heap.allocate(3);get(a)=1;get(a+1)=2;get(a+2)=3;
        uint32_t copied=0;
        auto copy=[&](uint32_t dst,uint32_t src,uint32_t n){copied=n;for(uint32_t i=0;i<n;++i)get(dst+i)=get(src+i);};
        b=heap.reallocate(a,64,copy);
        check(copied==3 && get(b)==1 && get(b+2)==3,"Growing realloc read beyond old requested size");
        c=heap.reallocate(b,2,copy);check(copied==2 && get(c+1)==2,"Shrinking realloc copied too much");
        reject([&]{heap.reallocate(c,0xffffffff,copy);});
        check(heap.allocation_size(c)==2 && get(c)==1,"Failed realloc lost original allocation");
        reject([&]{heap.reallocate(c,16,[](uint32_t,uint32_t,uint32_t){throw std::runtime_error("copy failure");});});
        check(heap.allocation_size(c)==2,"Copy exception freed original block");
        check(heap.reallocate(c,0,copy)==0,"Zero-size realloc did not free");
        reject([&]{heap.allocation_size(c);});
        a=heap.allocate(64);for(unsigned i=0;i<64;++i)get(a+i)=0xff;heap.release(a);
        b=heap.zeroed(8,8,[&](uint32_t p,uint32_t n){for(uint32_t i=0;i<n;++i)get(p+i)=0;});
        for(unsigned i=0;i<64;++i)check(get(b+i)==0,"calloc reused dirty memory");
        reject([&]{heap.zeroed(0x80000000u,8,[](uint32_t,uint32_t){});});
        heap.release(b);
        reject([&]{heap.zeroed(8,8,[](uint32_t,uint32_t){throw std::runtime_error("zero failure");});});
        check(heap.allocate(64)==b,"Failed calloc leaked reserved block");
        ds::GuestHeap full;full.allocate(32*1024*1024);full.allocate(32*1024*1024);
        reject([&]{full.allocate(16);});
        ds::GuestHeap crowded;std::vector<uint32_t> pointers;
        for(unsigned i=0;i<100000;++i)pointers.push_back(crowded.allocate(16));
        for(unsigned i=0;i<pointers.size();i+=2)crowded.release(pointers[i]);
        for(unsigned i=0;i<pointers.size()/2;++i)check(crowded.allocation_size(crowded.allocate(16))==16,"Indexed free blocks lost allocations");
        std::cout<<"PASS: canonical image bounds, nested stack ABI and exception restoration, heap reuse/coalescing, bounded realloc and zeroed calloc\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
