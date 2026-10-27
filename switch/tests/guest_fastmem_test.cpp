#include <dynarmic/interface/A32/a32.h>
#include <dynarmic/interface/A32/config.h>
#include <array>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

struct Fixture: Dynarmic::A32::UserCallbacks {
    static constexpr uint32_t base=0x30001000,code=0x10000000;
    std::array<uint8_t,4096> bytes{};
    std::unique_ptr<std::array<uint8_t*,1u<<20>> table=std::make_unique<std::array<uint8_t*,1u<<20>>();
    Dynarmic::A32::Jit* cpu=nullptr;unsigned reads=0,writes=0;uint64_t ticks=1000;bool guard=false;
    std::optional<uint32_t> MemoryReadCode(uint32_t p)override{const uint32_t words[]={0xe5941000,0xe2811001,0xe5841000,0xef000001};if(p<code||p-code>=sizeof(words))return std::nullopt;return words[(p-code)/4];}
    bool valid(uint32_t p,unsigned n){if(p<base||uint64_t(p)+n>uint64_t(base)+bytes.size()){guard=true;cpu->HaltExecution();return false;}return true;}
    uint8_t MemoryRead8(uint32_t p)override{return valid(p,1)?bytes[p-base]:0;}
    uint16_t MemoryRead16(uint32_t p)override{return uint16_t(MemoryRead8(p))|uint16_t(MemoryRead8(p+1))<<8;}
    uint32_t MemoryRead32(uint32_t p)override{++reads;uint32_t v=0;if(valid(p,4))std::memcpy(&v,bytes.data()+p-base,4);return v;}
    uint64_t MemoryRead64(uint32_t p)override{return MemoryRead32(p)|uint64_t(MemoryRead32(p+4))<<32;}
    void MemoryWrite8(uint32_t p,uint8_t v)override{if(valid(p,1))bytes[p-base]=v;}
    void MemoryWrite16(uint32_t p,uint16_t v)override{MemoryWrite8(p,uint8_t(v));MemoryWrite8(p+1,uint8_t(v>>8));}
    void MemoryWrite32(uint32_t p,uint32_t v)override{++writes;if(valid(p,4))std::memcpy(bytes.data()+p-base,&v,4);}
    void MemoryWrite64(uint32_t p,uint64_t v)override{MemoryWrite32(p,uint32_t(v));MemoryWrite32(p+4,uint32_t(v>>32));}
    void InterpreterFallback(uint32_t,size_t)override{throw std::runtime_error("Interpreter fallback");}
    void ExceptionRaised(uint32_t,Dynarmic::A32::Exception)override{throw std::runtime_error("CPU exception");}
    void CallSVC(uint32_t)override{cpu->HaltExecution();}
    void AddTicks(uint64_t n)override{ticks=n>ticks?0:ticks-n;}
    uint64_t GetTicksRemaining()override{return ticks;}
    void run(uint32_t p){ticks=1000;cpu->Reset();cpu->Regs()[4]=p;cpu->Regs()[15]=code;cpu->SetCpsr(0x10);cpu->Run();}
};
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
    Fixture f;(*f.table)[Fixture::base>>12]=f.bytes.data();f.bytes[0]=41;
    Dynarmic::A32::UserConfig config;config.callbacks=&f;config.page_table=f.table.get();config.detect_misaligned_access_via_page_table=8|16|32|64;config.only_detect_misalignment_via_page_table_on_page_boundary=true;
    Dynarmic::A32::Jit cpu(config);f.cpu=&cpu;f.run(Fixture::base);
    check(f.bytes[0]==42&&f.reads==0&&f.writes==0,"Mapped ARM load/store did not use direct page access");
    (*f.table)[Fixture::base>>12]=nullptr;f.run(Fixture::base);
    check(f.bytes[0]==43&&f.reads==1&&f.writes==1,"Unmapping retained a stale fast pointer");
    (*f.table)[Fixture::base>>12]=f.bytes.data();f.run(Fixture::base+4094);
    check(f.guard&&f.reads>=2,"Cross-page ARM access bypassed the guard callback");
    std::cout<<"PASS: actual ARM fast-page reads/writes, pointer removal and cross-page guard fallback\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
