// Real Dynarmic ARM32 context switching with synthetic producer/consumer code.
// No original game binary, loader whitelist bypass or gameplay assertion.
#include "guest_threads.hpp"
#include <dynarmic/interface/A32/a32.h>
#include <dynarmic/interface/A32/config.h>
#include <array>
#include <iostream>
#include <stdexcept>

struct Runtime: Dynarmic::A32::UserCallbacks {
    ds::GuestThreads threads;
    Dynarmic::A32::Jit* cpu=nullptr;
    uint64_t ticks=0;uint32_t event=0;bool finished=false;
    std::array<uint32_t,3> data{};
    const std::array<uint32_t,5> main_code={0xe3a00001,0xe5840000,0xef000002,0xe5940004,0xe12fff1e};
    const std::array<uint32_t,8> worker_code={0xe5941000,0xe3510001,0x0a000001,0xef000001,0xeafffffa,0xe3a0002a,0xe5840004,0xe12fff1e};
    uint32_t MemoryRead32(uint32_t p) override {
        if(p>=0x10000000&&p<0x10000000+main_code.size()*4)return main_code.at((p-0x10000000)/4);
        if(p>=0x10000100&&p<0x10000100+worker_code.size()*4)return worker_code.at((p-0x10000100)/4);
        if(p>=0x30000000&&p<0x3000000c)return data.at((p-0x30000000)/4);
        if(p==0x70000000)return 0xef00ffff;
        throw std::runtime_error("Unmapped synthetic ARM read");
    }
    uint8_t MemoryRead8(uint32_t p)override{return uint8_t(MemoryRead32(p&~3u)>>(8*(p&3)));}
    uint16_t MemoryRead16(uint32_t p)override{return MemoryRead8(p)|uint16_t(MemoryRead8(p+1))<<8;}
    uint64_t MemoryRead64(uint32_t p)override{return MemoryRead32(p)|uint64_t(MemoryRead32(p+4))<<32;}
    void MemoryWrite32(uint32_t p,uint32_t v)override {if(p<0x30000000||p>=0x3000000c||(p&3))throw std::runtime_error("Unmapped synthetic ARM write");data.at((p-0x30000000)/4)=v;}
    void MemoryWrite8(uint32_t,uint8_t)override{throw std::runtime_error("Unexpected byte write");}
    void MemoryWrite16(uint32_t,uint16_t)override{throw std::runtime_error("Unexpected halfword write");}
    void MemoryWrite64(uint32_t,uint64_t)override{throw std::runtime_error("Unexpected doubleword write");}
    void InterpreterFallback(uint32_t,size_t)override{throw std::runtime_error("Unexpected ARM fallback");}
    void ExceptionRaised(uint32_t,Dynarmic::A32::Exception)override{throw std::runtime_error("Unexpected ARM exception");}
    void CallSVC(uint32_t id)override{event=id;cpu->HaltExecution();}
    void AddTicks(uint64_t n)override{ticks=n>ticks?0:ticks-n;threads.tick(n*10);}
    uint64_t GetTicksRemaining()override{return ticks;}
};
int main(){try {
    Runtime r;Dynarmic::A32::UserConfig config;config.callbacks=&r;config.arch_version=Dynarmic::A32::ArchVersion::v6K;config.code_cache_size=32*1024*1024;
    Dynarmic::A32::Jit cpu(config);r.cpu=&cpu;
    auto& main=r.threads.thread();main.cpu.regs[4]=0x30000000;main.cpu.regs[13]=0x201fff00;main.cpu.regs[14]=0x70000000;main.cpu.regs[15]=0x10000000;
    for(unsigned i=0;i<64;++i)main.cpu.ext[i]=0x3f800000+i;
    const auto child=r.threads.create(0x10000100,0,1024*1024,false,0x70000000);r.threads.find(child)->cpu.regs[4]=0x30000000;
    // Worker starts before the parent publishes the flag; it must sleep/yield,
    // then observe the parent's actual ARM store, finish, and unblock the join.
    r.threads.select();r.threads.thread().cpu.load(cpu);
    unsigned switches=0,sleeps=0;
    for(unsigned runs=0;runs<1000&&!r.finished;++runs){
        r.ticks=100;r.event=0;cpu.Run();
        if(r.event==1){r.threads.sleep(100000);++sleeps;}
        else if(r.event==2){if(r.threads.join(child,0x30000008)!=0)throw std::runtime_error("ARM parent join failed");}
        else if(r.event==65535){if(r.threads.current()==1){r.finished=true;break;}r.threads.exit(cpu.Regs()[0]);}
        if(auto* old=r.threads.find(r.threads.current()))old->cpu.save(cpu);
        r.threads.reap_joins([&](auto p,auto v){r.MemoryWrite32(p,v);});
        const auto old=r.threads.current();r.threads.select();if(old!=r.threads.current())++switches;
        cpu.ClearExclusiveState();r.threads.thread().cpu.load(cpu);
    }
    if(!r.finished||cpu.Regs()[0]!=42||r.data[0]!=1||r.data[1]!=42||r.data[2]!=42||!sleeps||switches<3||cpu.ExtRegs()[31]!=0x3f80001f||cpu.Regs()[13]!=0x201fff00)throw std::runtime_error("ARM cooperative completion invariant failed");
    std::cout<<"PASS: actual ARM32 worker yielded, parent published flag, worker completed, join delivered 42; switches="<<switches<<" sleeps="<<sleeps<<"\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
