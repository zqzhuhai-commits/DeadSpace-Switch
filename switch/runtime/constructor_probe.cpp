#include "constructor_probe.hpp"
#include "guest_image.hpp"
#include <dynarmic/interface/A32/a32.h>
#include <dynarmic/interface/A32/config.h>
#include <algorithm>
#include <array>
#include <ctime>
#include <map>
#include <stdexcept>
#include <unordered_map>

namespace {
constexpr uint32_t heap=0x30000000, arena=0x40000000, arena_end=0x50000000;
constexpr uint32_t stack=0x20000000, thunks=0x60000000, stop=0x70000000;
class Runtime final: public Dynarmic::A32::UserCallbacks {
    using Page=std::array<uint8_t,4096>;
    std::unordered_map<uint32_t,Page> pages;
    std::vector<std::string> functions;
    std::unordered_map<uint32_t,uint32_t> allocations;
    struct Mutex {unsigned type=0,depth=0;};
    std::map<uint32_t,Mutex> mutexes;
    std::map<uint32_t,unsigned> attrs,semaphores;
    struct Destructor {uint32_t object,function,dso;};
    std::vector<Destructor> destructors;
    uint32_t next=heap,brk=arena,mmap_top=arena_end;
    uint32_t pending=0,errno_address=0;
    bool returned=false;
    uint64_t budget=0;
    std::string fault;
    bool mapped(uint32_t a) const {
        return (a>=stack && a-stack<2*1024*1024) || (a>=heap && a<next) ||
               (a>=arena && a<brk) || (a>=mmap_top && a<arena_end);
    }
    void fail(const std::string& message) {
        if(fault.empty())fault=message;
        if(cpu)cpu->HaltExecution();
        budget=0;
    }
    uint32_t alloc(uint32_t size) {
        const uint64_t n=(uint64_t(size)+15)&~uint64_t(15);
        if(n>32*1024*1024 || uint64_t(next)+std::max<uint64_t>(n,16)>heap+32*1024*1024)throw std::runtime_error("Constructor heap limit");
        const auto p=next;next+=std::max<uint64_t>(n,16);allocations[p]=next-p;return p;
    }
    uint32_t arg(unsigned n) {return n<4?cpu->Regs()[n]:MemoryRead32(cpu->Regs()[13]+4*(n-4));}
    void bounded(uint32_t n) {if(n>32*1024*1024)throw std::runtime_error("Memory operation limit");}
    void service(const std::string& name) {
        const auto a=arg(0),b=arg(1),c=arg(2),d=arg(3);
        uint32_t result=0;
        if(name=="memset") {bounded(c);for(uint32_t i=0;i<c;++i)MemoryWrite8(a+i,b);result=a;}
        else if(name=="memcpy" || name=="memmove") {
            bounded(c);std::vector<uint8_t> data(c);for(uint32_t i=0;i<c;++i)data[i]=MemoryRead8(b+i);
            for(uint32_t i=0;i<c;++i)MemoryWrite8(a+i,data[i]);
            result=a;
        }
        else if(name=="malloc" || name=="calloc") {
            const uint64_t n=name=="malloc"?a:uint64_t(a)*b;
            if(n>32*1024*1024)throw std::runtime_error("Allocation size limit");
            result=alloc(n);
        }
        else if(name=="free") {if(a && !allocations.count(a))throw std::runtime_error("Invalid free");}
        else if(name=="__errno")result=errno_address;
        else if(name=="sbrk") {
            const int64_t end=int64_t(brk)+int32_t(a);
            if(end<arena || end>mmap_top) {MemoryWrite32(errno_address,12);result=0xffffffff;}
            else {result=brk;brk=end;}
        }
        else if(name=="mmap") {
            if(a || c!=3 || d!=0x22 || arg(4)!=0xffffffff || arg(5))throw std::runtime_error("Unsupported constructor mmap");
            const uint64_t size=(uint64_t(b)+4095)&~uint64_t(4095);
            if(!size || size>mmap_top-brk){MemoryWrite32(errno_address,12);result=0xffffffff;}
            else {mmap_top-=size;result=mmap_top;}
        }
        else if(name=="__aeabi_atexit")destructors.push_back({a,b,c});
        else if(name=="__cxa_guard_acquire")result=MemoryRead8(a)==0;
        else if(name=="__cxa_guard_release")MemoryWrite8(a,1);
        else if(name=="clock")result=uint64_t(std::clock())*1000000/CLOCKS_PER_SEC;
        else if(name=="time") {result=std::time(nullptr);if(a)MemoryWrite32(a,result);}
        else if(name=="pthread_mutexattr_init")attrs[a]=0;
        else if(name=="pthread_mutexattr_settype") {if(b>2 || !attrs.count(a))throw std::runtime_error("Invalid mutex attribute");attrs[a]=b;}
        else if(name=="pthread_mutexattr_setpshared") {if(b || !attrs.count(a))throw std::runtime_error("Shared mutex unsupported");}
        else if(name=="pthread_mutexattr_destroy") {if(!attrs.erase(a))throw std::runtime_error("Unknown mutex attribute");}
        else if(name=="pthread_mutex_init") {if(b && !attrs.count(b))throw std::runtime_error("Unknown mutex attribute");mutexes[a]={b?attrs.at(b):0,0};}
        else if(name=="pthread_mutex_lock" || name=="pthread_mutex_trylock") {
            auto& m=mutexes[a];
            if(m.depth && m.type!=1) {
                if(name=="pthread_mutex_trylock")result=16;
                else throw std::runtime_error("Contended mutex requires guest scheduler");
            } else ++m.depth;
        }
        else if(name=="pthread_mutex_unlock") {auto& m=mutexes.at(a);if(!m.depth)throw std::runtime_error("Unlock unowned mutex");--m.depth;}
        else if(name=="pthread_mutex_destroy") {if(mutexes.at(a).depth)result=16;else mutexes.erase(a);}
        else if(name=="pthread_self")result=1;
        else if(name=="sem_init") {if(b || c>0x7fffffff)throw std::runtime_error("Unsupported semaphore attributes");semaphores[a]=c;}
        else if(name=="sem_destroy") {if(!semaphores.erase(a))throw std::runtime_error("Unknown semaphore");}
        else if(name=="sem_post") {auto& value=semaphores.at(a);if(value==0x7fffffff)throw std::runtime_error("Semaphore overflow");++value;}
        else if(name=="__atomic_swap") {result=MemoryRead32(b);MemoryWrite32(b,a);}
        else if(name=="__atomic_inc" || name=="__atomic_dec") {result=MemoryRead32(a);MemoryWrite32(a,result+(name=="__atomic_inc"?1:-1));}
        else if(name=="__atomic_cmpxchg") {result=MemoryRead32(c)!=a;if(!result)MemoryWrite32(c,b);}
        else throw std::runtime_error("Unsupported import: "+name);
        cpu->Regs()[0]=result;
        const auto lr=cpu->Regs()[14];cpu->Regs()[15]=lr&~1u;
        cpu->SetCpsr((cpu->Cpsr()&~0x20u)|((lr&1)?0x20u:0));
    }
public:
    ds::GuestImage image;
    Dynarmic::A32::Jit* cpu=nullptr;
    unsigned completed=0,service_calls=0;
    uint32_t bind(const ds::GuestImage::Import& item) {
        if(item.type!=1) {functions.push_back(item.name);return thunks+4*(functions.size()-1);}
        const auto p=alloc(std::max<uint32_t>(item.size,4096));
        if(item.name=="__stack_chk_guard")MemoryWrite32(p,0xa5a55a5a);
        else if(item.name=="__page_size")MemoryWrite32(p,4096);
        else if(item.name=="_tolower_tab_" || item.name=="_toupper_tab_") {
            const auto table=alloc(514);MemoryWrite16(table,0xffff);
            for(unsigned c=0;c<256;++c) {
                const auto v=item.name=="_tolower_tab_" && c>=65 && c<=90?c+32:item.name=="_toupper_tab_" && c>=97 && c<=122?c-32:c;
                MemoryWrite16(table+2*(c+1),v);
            }
            MemoryWrite32(p,table);
        } else if(item.name=="_ctype_") {
            const auto table=alloc(257);
            for(unsigned c=0;c<256;++c) {
                unsigned f=(c>=65&&c<=90?1:0)|(c>=97&&c<=122?2:0)|(c>=48&&c<=57?4:0);
                if(c==32 || (c>=9 && c<=13))f|=8;
                if(c<32 || c==127)f|=32;
                if((c>=48&&c<=57)||(c>=65&&c<=70)||(c>=97&&c<=102))f|=64;
                if(c==32)f|=128;
                if(c>=33&&c<=126&&!(f&7))f|=16;
                MemoryWrite8(table+c+1,f);
            }
            MemoryWrite32(p,table);
        } else if(item.name!="__sF" && item.name!="__dso_handle" && item.name!="tzname")throw std::runtime_error("Unknown imported object: "+item.name);
        return p;
    }
    void initialize() {
        errno_address=alloc(4);
        std::string error;if(!image.bind_imports([&](const auto& i){return bind(i);},error))throw std::runtime_error(error);
        if(!fault.empty())throw std::runtime_error(fault);
    }
    uint8_t MemoryRead8(uint32_t a) override {
        if(mapped(a)) {const auto it=pages.find(a>>12);return it==pages.end()?0:it->second[a&4095];}
        if(a>=thunks && uint64_t(a)-thunks<functions.size()*4) return uint8_t((0xef000001u+((a-thunks)/4))>>(8*((a-thunks)%4)));
        if(a>=stop && a-stop<4)return uint8_t(0xef00ffffu>>(8*(a-stop)));
        uint8_t v=0;if(!image.read(a,&v,1))fail("Unmapped guest read at "+std::to_string(a));return v;
    }
    uint16_t MemoryRead16(uint32_t a) override {return MemoryRead8(a)|uint16_t(MemoryRead8(a+1))<<8;}
    uint32_t MemoryRead32(uint32_t a) override {return MemoryRead16(a)|uint32_t(MemoryRead16(a+2))<<16;}
    uint64_t MemoryRead64(uint32_t a) override {return MemoryRead32(a)|uint64_t(MemoryRead32(a+4))<<32;}
    std::optional<uint32_t> MemoryReadCode(uint32_t a) override {
        if(a!=stop && !(a>=thunks && uint64_t(a)-thunks+4<=functions.size()*4) && !image.executable(a,4)) {fail("Unmapped guest execution");return std::nullopt;}
        return MemoryRead32(a);
    }
    void MemoryWrite8(uint32_t a,uint8_t v) override {
        if(mapped(a)) {pages[a>>12][a&4095]=v;return;}
        if(!image.write(a,&v,1))fail("Write outside writable guest mapping at "+std::to_string(a));
    }
    void MemoryWrite16(uint32_t a,uint16_t v) override {MemoryWrite8(a,v);MemoryWrite8(a+1,v>>8);}
    void MemoryWrite32(uint32_t a,uint32_t v) override {MemoryWrite16(a,v);MemoryWrite16(a+2,v>>16);}
    void MemoryWrite64(uint32_t a,uint64_t v) override {MemoryWrite32(a,v);MemoryWrite32(a+4,v>>32);}
    void InterpreterFallback(uint32_t,size_t) override {fail("Unavailable instruction interpreter");}
    void ExceptionRaised(uint32_t,Dynarmic::A32::Exception) override {fail("Guest CPU exception");}
    void CallSVC(uint32_t id) override {if(id==65535)returned=true;else pending=id;cpu->HaltExecution();}
    void AddTicks(uint64_t n) override {budget=n>budget?0:budget-n;}
    uint64_t GetTicksRemaining() override {return budget;}
    void run() {
        for(const auto fn:image.constructors) {
            if(!image.executable(fn&~1u,4))throw std::runtime_error("Invalid constructor address");
            cpu->Reset();cpu->Regs().fill(0);cpu->Regs()[13]=stack+2*1024*1024-256;
            cpu->Regs()[14]=stop;cpu->Regs()[15]=fn&~1u;cpu->SetCpsr((fn&1)?0x30:0x10);cpu->SetFpscr(0);
            budget=1000000;returned=false;pending=0;
            unsigned calls=0;
            while(!returned && fault.empty() && budget) {
                cpu->Run();
                if(pending) {
                    const auto id=pending;pending=0;
                    if(id>functions.size() || ++calls>10000)throw std::runtime_error("Import dispatch limit");
                    ++service_calls;service(functions[id-1]);
                }
            }
            if(!fault.empty())throw std::runtime_error(fault);
            if(!returned)throw std::runtime_error("Constructor instruction budget exhausted");
            ++completed;
        }
    }
};
}
std::string constructor_probe(const std::string& path) {
    Runtime env;
    try {
        std::string error;if(!env.image.load(path,error))throw std::runtime_error(error);
        env.initialize();
        Dynarmic::A32::UserConfig conf;conf.callbacks=&env;conf.arch_version=Dynarmic::A32::ArchVersion::v6K;
        conf.code_cache_size=16*1024*1024;conf.page_table=nullptr;conf.fastmem_pointer=std::nullopt;
        Dynarmic::A32::Jit cpu(conf);env.cpu=&cpu;env.run();
        return "PASS: "+std::to_string(env.completed)+" actual game constructors returned; "+std::to_string(env.service_calls)+" import calls.\nSingle-thread constructor runtime only. JNI/game loop not initialized.\n";
    } catch(const std::exception& e) {
        return "BLOCKED after "+std::to_string(env.completed)+" constructors: "+e.what()+"\n";
    }
}
