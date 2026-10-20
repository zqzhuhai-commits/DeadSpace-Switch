#include "cpu_probe.hpp"
#include "guest_image.hpp"
#include <array>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <dynarmic/interface/A32/a32.h>
#include <dynarmic/interface/A32/config.h>

namespace {
constexpr uint32_t return_address=0x70000000,stack_base=0x20000000;
constexpr uint32_t test_address=0x71000000;
class Environment final: public Dynarmic::A32::UserCallbacks {
public:
    ds::GuestImage image;
    std::array<uint8_t,65536> stack{};
    std::array<uint8_t,16> test_code{};
    Dynarmic::A32::Jit* cpu=nullptr;
    uint64_t ticks=0;
    bool returned=false;
    std::string fault;
    void fail(const char* message) { if(fault.empty())fault=message;if(cpu)cpu->HaltExecution();ticks=0; }
    uint8_t MemoryRead8(uint32_t address) override {
        if(address>=stack_base && address-stack_base<stack.size())return stack[address-stack_base];
        if(address>=test_address && address-test_address<test_code.size())return test_code[address-test_address];
        if(address>=return_address && address<return_address+4)return uint8_t(0xef0000ffu>>(8*(address-return_address)));
        uint8_t value=0;if(!image.read(address,&value,1))fail("Guest read outside mapped memory");return value;
    }
    uint16_t MemoryRead16(uint32_t a) override { return MemoryRead8(a)|uint16_t(MemoryRead8(a+1))<<8; }
    uint32_t MemoryRead32(uint32_t a) override { return MemoryRead16(a)|uint32_t(MemoryRead16(a+2))<<16; }
    uint64_t MemoryRead64(uint32_t a) override { return MemoryRead32(a)|uint64_t(MemoryRead32(a+4))<<32; }
    std::optional<uint32_t> MemoryReadCode(uint32_t a) override {
        if(a!=return_address && !(a>=test_address && a-test_address+4<=test_code.size()) && !image.executable(a,4)) { fail("Guest execute outside code segment");return std::nullopt; }
        return MemoryRead32(a);
    }
    void MemoryWrite8(uint32_t a,uint8_t v) override {
        if(a>=stack_base && a-stack_base<stack.size()){stack[a-stack_base]=v;return;}
        if(!image.write(a,&v,1))fail("Guest write outside writable memory");
    }
    void MemoryWrite16(uint32_t a,uint16_t v) override { MemoryWrite8(a,v);MemoryWrite8(a+1,v>>8); }
    void MemoryWrite32(uint32_t a,uint32_t v) override { MemoryWrite16(a,v);MemoryWrite16(a+2,v>>16); }
    void MemoryWrite64(uint32_t a,uint64_t v) override { MemoryWrite32(a,v);MemoryWrite32(a+4,v>>32); }
    void InterpreterFallback(uint32_t,size_t) override { fail("Instruction needs an unavailable interpreter fallback"); }
    void CallSVC(uint32_t swi) override { if(swi==255){returned=true;cpu->HaltExecution();ticks=0;}else fail("Unexpected guest SVC"); }
    void ExceptionRaised(uint32_t,Dynarmic::A32::Exception) override { fail("Guest CPU exception"); }
    void AddTicks(uint64_t n) override { ticks=n>ticks?0:ticks-n; }
    uint64_t GetTicksRemaining() override { return ticks; }
};
}

std::string cpu_probe(const std::string& path) {
    try {
        Environment env;
        std::string error;
        if(!env.image.load(path,error))return "ARM32 probe: "+error+"\n";
        Dynarmic::A32::UserConfig conf;
        conf.callbacks=&env;
        conf.arch_version=Dynarmic::A32::ArchVersion::v6K;
        conf.code_cache_size=8*1024*1024;
        conf.page_table=nullptr;
        conf.fastmem_pointer=std::nullopt;
        Dynarmic::A32::Jit cpu(conf);env.cpu=&cpu;
        struct Case {const char* name;uint32_t expected;};
        const Case cases[]={
          {"NativeGetIdRawPointerCancel",12},
          {"NativeGetIdRawPointerDown",0x6000c},
          {"NativeGetIdRawPointerMove",0x4000c},
          {"NativeGetIdUndefined",0},
          {"NativeGetIdRawPointerUp",0x8000c}
        };
        std::string report="Dynarmic ARM32 actual-donor leaf-function probe\n";
        report+="Guest mapped, relative relocations="+std::to_string(env.image.relative_relocations)+"\n";
        for(const auto& c:cases) {
            const std::string symbol="Java_com_ea_blast_TouchSurfaceAndroid_"+std::string(c.name);
            const auto it=env.image.exports.find(symbol);
            if(it==env.image.exports.end())return report+"FAIL: export missing\n";
            cpu.Reset();env.returned=false;env.fault.clear();env.ticks=100;
            cpu.Regs().fill(0);
            cpu.Regs()[13]=stack_base+env.stack.size()-16;
            cpu.Regs()[14]=return_address;
            cpu.Regs()[15]=it->second&~1u;
            cpu.SetCpsr((it->second&1)?0x30:0x10);
            cpu.Run();
            if(!env.fault.empty())return report+"FAIL: "+env.fault+"\n";
            if(!env.returned)return report+"FAIL: guest did not return within tick budget\n";
            if(cpu.Regs()[0]!=c.expected)return report+"FAIL: unexpected return value for "+c.name+"\n";
            report+=std::string(c.name)+" = "+std::to_string(cpu.Regs()[0])+" OK\n";
        }
        auto reset=[&](uint32_t pc,bool thumb) {
            cpu.Reset();env.returned=false;env.fault.clear();env.ticks=100;
            cpu.Regs().fill(0);cpu.Regs()[13]=stack_base+env.stack.size()-16;
            cpu.Regs()[14]=return_address;cpu.Regs()[15]=pc;cpu.SetCpsr(thumb?0x30:0x10);
        };
        // Independent synthetic tests cover Thumb interworking and VFP, not game progress.
        env.test_code={0x88,0x00,0x70,0x47}; // lsls r0,r1,#2; bx lr
        reset(test_address,true);cpu.Regs()[1]=2;cpu.Run();
        if(!env.returned || !env.fault.empty() || cpu.Regs()[0]!=8)return report+"FAIL: Thumb/interworking fixture\n";
        report+="Thumb shift and return to ARM sentinel: OK\n";
        env.test_code={0x20,0x0a,0x30,0xee,0x1e,0xff,0x2f,0xe1}; // vadd.f32 s0,s0,s1; bx lr
        cpu.ClearCache();reset(test_address,false);cpu.SetFpscr(0);
        cpu.ExtRegs()[0]=0x3fa00000;cpu.ExtRegs()[1]=0x40200000;cpu.Run();
        if(!env.returned || !env.fault.empty() || cpu.ExtRegs()[0]!=0x40700000)return report+"FAIL: VFP fixture\n";
        report+="VFP add 1.25 + 2.5 = 3.75: OK\n";
        reset(0xa0000000,false);cpu.Run();
        if(env.fault.empty())return report+"FAIL: unmapped code was accepted\n";
        report+="Unmapped execute rejection: OK\n";
        return report+"PASS: 5/5 real game functions executed. Game lifecycle/JNI services NOT initialized.\n";
    } catch(const std::exception& e) {return std::string("ARM32 probe failed: ")+e.what()+"\n";}
}
