#pragma once
#include "guest_address.hpp"
#include <array>
#include <stdexcept>
#include <string>

namespace ds {
inline uint32_t guest_call_sp(uint32_t current,unsigned argument_count,bool top_level) {
    constexpr uint32_t bottom=GuestLayout::stack_base;
    constexpr uint32_t top=bottom+GuestLayout::stack_size;
    if(argument_count>64)throw std::runtime_error("Guest call argument limit");
    const uint32_t arguments=argument_count>4?(argument_count-4)*4:0;
    const uint32_t parent=top_level?top:current;
    if(parent<bottom || parent>top || (!top_level && (parent&7)))throw std::runtime_error("Invalid parent guest SP");
    const uint32_t reserve=256+arguments;
    if(parent-bottom<reserve+64*1024)throw std::runtime_error("Nested guest stack limit");
    return (parent-reserve)&~7u;
}
// Snapshot both architectural state and dispatcher bookkeeping. Nested calls
// must restore the suspended caller even when a host service throws.
template<class Cpu> class GuestCallScope {
    Cpu& cpu;
    std::array<uint32_t,16> regs;
    std::array<uint32_t,64> ext;
    uint32_t cpsr,fpscr;
    bool& returned;
    uint32_t& pending;
    uint64_t& budget;
    std::string& fault;
    bool old_returned;
    uint32_t old_pending;
    uint64_t old_budget;
    std::string old_fault;
    bool preserve_cpu=false;
public:
    GuestCallScope(Cpu& c,bool& r,uint32_t& p,uint64_t& b,std::string& f):
        cpu(c),regs(c.Regs()),ext(c.ExtRegs()),cpsr(c.Cpsr()),fpscr(c.Fpscr()),
        returned(r),pending(p),budget(b),fault(f),old_returned(r),old_pending(p),old_budget(b),old_fault(f) {}
    GuestCallScope(const GuestCallScope&)=delete;
    GuestCallScope& operator=(const GuestCallScope&)=delete;
    void keep_top_level_result(){preserve_cpu=true;}
    ~GuestCallScope() {
        if(!preserve_cpu){cpu.Regs()=regs;cpu.ExtRegs()=ext;cpu.SetCpsr(cpsr);cpu.SetFpscr(fpscr);}
        returned=old_returned;pending=old_pending;budget=old_budget;fault.swap(old_fault);
    }
};
}
