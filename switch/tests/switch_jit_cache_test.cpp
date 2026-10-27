#include <oaknut/code_block.hpp>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
struct Op {const void* address;size_t size;bool instruction;};
static std::vector<Op> ops;
static JitType selected=JitType_CodeMemory;
static Jit* active=nullptr;
static unsigned transitions=0;
static void check(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
Result jitCreate(Jit* j,size_t n){*j={selected,std::calloc(1,n),nullptr,n,false};j->rx_addr=selected==JitType_CodeMemory?std::calloc(1,n):j->rw_addr;active=j;return !j->rw_addr||!j->rx_addr;}
Result jitClose(Jit* j){if(j->rx_addr!=j->rw_addr)std::free(j->rx_addr);std::free(j->rw_addr);active=nullptr;return 0;}
void* jitGetRwAddr(Jit* j){return j->rw_addr;}
void* jitGetRxAddr(Jit* j){return j->rx_addr;}
Result jitTransitionToExecutable(Jit* j){++transitions;j->is_executable=true;return 0;}
Result jitTransitionToWritable(Jit* j){j->is_executable=false;return 0;}
void armDCacheFlush(const void* p,size_t n){ops.push_back({p,n,false});}
void armICacheInvalidate(const void* p,size_t n){
    auto offset=reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(active->rx_addr);
    check(!ops.empty()&&!ops.back().instruction,"I-cache invalidated before data clean");
    check(ops.back().address==static_cast<uint8_t*>(active->rw_addr)+offset&&ops.back().size==n,"Alias clean range mismatch");
    std::memcpy(static_cast<uint8_t*>(active->rx_addr)+offset,static_cast<uint8_t*>(active->rw_addr)+offset,n);ops.push_back({p,n,true});
}
int main(){try{
    {oaknut::CodeBlock block(32*1024*1024);block.wptr()[16]=0x11223344;block.invalidate(block.ptr()+16,16);block.protect();
     check(transitions==0,"CodeMemory protect performed full-buffer transition");
     check(ops.size()==2&&ops[0].size==16&&block.ptr()[16]==0x11223344,"New code range not synchronized");
     block.unprotect();block.wptr()[17]=0xaabbccdd;block.invalidate(block.ptr()+17,4);block.protect();
     check(transitions==0&&ops.size()==4&&ops[2].size==4&&block.ptr()[17]==0xaabbccdd,"Existing relinked code not synchronized");
     auto reject=[&](auto* p,size_t n){bool caught=false;try{block.invalidate(p,n);}catch(const std::runtime_error&){caught=true;}check(caught,"Invalid cache range accepted");};
     reject(reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(block.ptr())-4),4);reject(block.ptr(),32*1024*1024+1);
     block.invalidate(block.ptr()+32*1024*1024/4,0);check(ops.size()==4,"Empty range caused cache work");}
    selected=JitType_SetProcessMemoryPermission;
    {oaknut::CodeBlock block(4096);block.invalidate(block.ptr(),4);block.protect();check(transitions==1,"Legacy mapping transition omitted");}
    std::cout<<"PASS: separate RW/RX cache ranges, relink updates, no whole-buffer CodeMemory flush, bounds and legacy transition (mock libnx API)\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
