#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <algorithm>
#include <limits>

namespace ds {
struct GuestCpuState {
    std::array<uint32_t,16> regs{};
    std::array<uint32_t,64> ext{};
    uint32_t cpsr=0x10,fpscr=0;
    template<class Cpu> void save(Cpu& cpu){regs=cpu.Regs();ext=cpu.ExtRegs();cpsr=cpu.Cpsr();fpscr=cpu.Fpscr();}
    template<class Cpu> void load(Cpu& cpu)const{cpu.Regs()=regs;cpu.ExtRegs()=ext;cpu.SetCpsr(cpsr);cpu.SetFpscr(fpscr);}
};
// One guest CPU, round-robin contexts, shared guest memory. Blocking imports
// resume at their saved return address only after their resource becomes ready.
class GuestThreads {
public:
    enum class State {ready, sleep, semaphore, mutex, monitor, join, exited};
    struct Thread {
        GuestCpuState cpu;
        State state=State::ready;
        uint32_t id=0,stack_begin=0,stack_size=0,wait_key=0,join_result=0,joiner=0,result=0,exception=0,error=0;
        uint64_t wake=0;
        bool detached=false,timed_wait=false,timed_out=false;
        std::map<uint32_t,uint32_t> tls;
    };
private:
    std::map<uint32_t,Thread> tasks;
    std::array<uint32_t,32> stacks{};
    uint32_t next_id=2,active=1;
    uint64_t now=0;
    void release_stack(Thread& t){if(t.stack_begin>=0x21000000u){stacks.at((t.stack_begin-0x21000000u)/0x200000u)=0;t.stack_size=0;}}
public:
    GuestThreads(){Thread main;main.id=1;main.stack_begin=0x20000000;main.stack_size=0x200000;tasks.emplace(1,main);}
    uint32_t current()const{return active;}
    Thread& thread(){return tasks.at(active);}
    const Thread& thread()const{return tasks.at(active);}
    Thread* find(uint32_t id){auto it=tasks.find(id);return it==tasks.end()?nullptr:&it->second;}
    uint32_t create(uint32_t entry,uint32_t arg,uint32_t size,bool detached,uint32_t stop){
        if(!entry||size<16384||size>0x200000||next_id>=4096)throw std::runtime_error("Guest thread parameters/limit");
        auto slot=std::find(stacks.begin(),stacks.end(),0u);
        if(slot==stacks.end())throw std::runtime_error("Guest thread stack limit");
        Thread t;t.id=next_id++;t.detached=detached;
        t.stack_begin=0x21000000+uint32_t(slot-stacks.begin())*0x200000;
        t.stack_size=(size+4095)&~4095u;
        t.cpu.regs[0]=arg;t.cpu.regs[13]=(t.stack_begin+t.stack_size-256)&~7u;
        t.cpu.regs[14]=stop;t.cpu.regs[15]=entry&~1u;t.cpu.cpsr=(entry&1)?0x30:0x10;
        const auto id=t.id;tasks.emplace(id,t);*slot=id;return id;
    }
    bool mapped(uint32_t p)const {
        if(p>=0x20000000u&&p<0x20200000u)return true;
        if(p<0x21000000u||p>=0x25000000u)return false;
        const auto id=stacks[(p-0x21000000u)/0x200000u];
        const auto it=tasks.find(id);
        return it!=tasks.end()&&uint64_t(p)-it->second.stack_begin<it->second.stack_size;
    }
    void block(State state,uint32_t key){auto& t=thread();t.state=state;t.wait_key=key;t.timed_wait=false;t.timed_out=false;}
    void wait_until(State state,uint32_t key,uint64_t deadline){block(state,key);thread().wake=deadline;thread().timed_wait=true;}
    void sleep(uint64_t nanos){if(nanos>std::numeric_limits<uint64_t>::max()-now)throw std::runtime_error("Guest sleep overflow");thread().state=State::sleep;thread().wake=now+nanos;}
    void tick(uint64_t nanos){now=std::min(std::numeric_limits<uint64_t>::max()-nanos,now)+nanos;}
    uint64_t time()const{return now;}
    uint32_t waiter(State state,uint32_t key)const{for(const auto& kv:tasks)if(kv.second.state==state&&kv.second.wait_key==key)return kv.first;return 0;}
    void wake(uint32_t id){auto& t=tasks.at(id);if(t.state==State::exited)throw std::runtime_error("Wake exited guest thread");t.state=State::ready;t.wait_key=0;t.timed_wait=false;}
    int join(uint32_t id,uint32_t result_pointer){
        auto* target=find(id);if(!target)return 3;
        if(id==active)return 35;
        if(target->detached||target->joiner)return 22;
        target->joiner=active;
        thread().join_result=result_pointer;
        block(State::join,id);return 0;
    }
    int detach(uint32_t id){auto* t=find(id);if(!t)return 3;if(t->detached||t->joiner)return 22;t->detached=true;if(t->state==State::exited){release_stack(*t);tasks.erase(id);}return 0;}
    template<class Write> void reap_joins(Write write){
        for(auto it=tasks.begin();it!=tasks.end();){auto& target=it->second;
            if(target.state==State::exited&&target.joiner){auto& parent=tasks.at(target.joiner);if(parent.state!=State::join||parent.wait_key!=target.id)throw std::runtime_error("Guest join state mismatch");if(parent.join_result)write(parent.join_result,target.result);parent.join_result=0;wake(parent.id);release_stack(target);it=tasks.erase(it);}else ++it;
        }
    }
    void exit(uint32_t result){auto& t=thread();t.result=result;t.state=State::exited;if(t.detached){release_stack(t);tasks.erase(active);}}
    // Called after saving the old CPU. Sleeping threads advance virtual time
    // only when every runnable thread has yielded; unsatisfiable waits fail.
    uint32_t select(){
        auto ready=[&](){for(auto& kv:tasks){auto& t=kv.second;
            if(t.wake>now)continue;
            if(t.state==State::sleep)t.state=State::ready;
            else if(t.timed_wait){t.state=State::ready;t.wait_key=0;t.timed_wait=false;t.timed_out=true;t.cpu.regs[0]=UINT32_MAX;}
        }};
        ready();
        auto pick=[&]()->uint32_t{for(auto it=tasks.upper_bound(active);it!=tasks.end();++it)if(it->second.state==State::ready)return it->first;for(auto& kv:tasks)if(kv.second.state==State::ready)return kv.first;return 0;};
        auto chosen=pick();
        if(!chosen){uint64_t earliest=std::numeric_limits<uint64_t>::max();for(const auto& kv:tasks)if(kv.second.state==State::sleep||kv.second.timed_wait)earliest=std::min(earliest,kv.second.wake);if(earliest!=std::numeric_limits<uint64_t>::max()){now=std::max(now,earliest);ready();chosen=pick();}}
        if(!chosen)throw std::runtime_error("Guest scheduler deadlock: no runnable thread");
        active=chosen;return chosen;
    }
};
}
