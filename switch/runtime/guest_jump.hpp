#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <stdexcept>
namespace ds {
// Opaque guest buffer tokens, never host libc's jmp_buf layout. Both imported
// endpoints use this bridge. Register snapshots are isolated by guest thread.
class GuestJumps {
    struct State {std::array<uint32_t,11> regs;std::array<uint32_t,16> vfp;uint32_t fpscr,owner,depth;};
    std::map<uint32_t,State> states;
    uint32_t next=0x4a000001;
public:
    void clear_owner(uint32_t owner){for(auto it=states.begin();it!=states.end();)if(it->second.owner==owner)it=states.erase(it);else ++it;}
    template<class Cpu,class Write> void save(Cpu& cpu,uint32_t p,uint32_t owner,uint32_t depth,Write write){
        if(!p||(p&3)||p>UINT32_MAX-3)throw std::runtime_error("Invalid guest jump buffer");
        if(states.size()>=4096||next==UINT32_MAX)throw std::runtime_error("Guest jump snapshot limit");
        State state{};for(unsigned i=0;i<11;++i)state.regs[i]=cpu.Regs()[i+4];
        for(unsigned i=0;i<16;++i)state.vfp[i]=cpu.ExtRegs()[i+16];
        state.fpscr=cpu.Fpscr();state.owner=owner;state.depth=depth;
        const auto id=next++;write(p,id);states.emplace(id,state);
    }
    template<class Cpu,class Read> void restore(Cpu& cpu,uint32_t p,uint32_t value,uint32_t owner,uint32_t depth,Read read){
        if(!p||(p&3)||p>UINT32_MAX-3)throw std::runtime_error("Invalid guest jump buffer");
        auto it=states.find(read(p));if(it==states.end())throw std::runtime_error("Expired guest jump buffer");const auto& state=it->second;
        if(state.owner!=owner||state.depth!=depth)throw std::runtime_error("Guest longjmp crosses thread/host callback boundary");
        for(unsigned i=0;i<11;++i)cpu.Regs()[i+4]=state.regs[i];
        for(unsigned i=0;i<16;++i)cpu.ExtRegs()[i+16]=state.vfp[i];
        cpu.SetFpscr(state.fpscr);cpu.Regs()[0]=value?value:1;
    }
};
}
