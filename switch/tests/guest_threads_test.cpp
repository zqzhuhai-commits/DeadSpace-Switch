#include "guest_threads.hpp"
#include "guest_jump.hpp"
#include <iostream>
#include <stdexcept>
#include <map>
using T=ds::GuestThreads;
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F> void reject(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}require(caught,"Invalid thread operation accepted");}
struct Cpu {ds::GuestCpuState state;auto& Regs(){return state.regs;}auto& ExtRegs(){return state.ext;}uint32_t Cpsr(){return state.cpsr;}uint32_t Fpscr(){return state.fpscr;}void SetCpsr(uint32_t v){state.cpsr=v;}void SetFpscr(uint32_t v){state.fpscr=v;}};
int main(){try{
 T s;Cpu cpu;
 for(unsigned i=0;i<16;++i)cpu.Regs()[i]=0x100+i;
 for(unsigned i=0;i<64;++i)cpu.ExtRegs()[i]=0x200+i;
 cpu.SetCpsr(0x30);cpu.SetFpscr(0x1234);s.thread().cpu.save(cpu);
 s.thread().tls[9]=42;s.thread().exception=71;s.thread().error=0x30001000;
 const auto a=s.create(0x10001001,7,1024*1024,false,0x70000000);
 const auto b=s.create(0x10002000,8,65536,true,0x70000000);
 const auto main=s.thread().cpu;const auto sa=s.find(a)->stack_begin;
 require(s.mapped(sa)&&!s.mapped(sa+1024*1024)&&!s.mapped(sa-1),"Thread stack range/guard incorrect");
 require(s.select()==a,"Round robin omitted first worker");s.thread().cpu.load(cpu);
 require(cpu.Regs()[0]==7&&cpu.Regs()[15]==0x10001000&&cpu.Cpsr()==0x30,"Thumb worker entry context invalid");
 require(s.thread().tls.empty()&&s.thread().exception==0,"Worker inherited main TLS/exception");
 s.thread().tls[9]=99;s.block(T::State::semaphore,0x1000);
 require(s.select()==b,"Blocked worker ran again");s.sleep(1234567);
 require(s.select()==1,"Main did not resume while workers wait");s.thread().cpu.load(cpu);
 require(cpu.state.regs==main.regs&&cpu.state.ext==main.ext&&cpu.Cpsr()==main.cpsr&&cpu.Fpscr()==main.fpscr,"Context switch lost ARM/VFP state");
 require(s.thread().tls[9]==42&&s.thread().exception==71&&s.thread().error==0x30001000,"Main thread-local state damaged");
 require(s.waiter(T::State::semaphore,0x1000)==a,"Semaphore waiter not found");s.wake(a);
 require(s.join(a,0x30002000)==0,"Main join failed");
 require(s.select()==a,"Join prevented worker progress");s.exit(0xabc);
 std::map<uint32_t,uint32_t> writes;s.reap_joins([&](auto p,auto v){writes[p]=v;});
 require(!s.find(a)&&writes[0x30002000]==0xabc&&!s.mapped(sa),"Join did not deliver result/release stack");
 require(s.select()==1,"Join did not wake main");
 require(s.join(1,0)==35&&s.join(9999,0)==3&&s.join(b,0)==22,"Join errors incorrect");
 const auto c=s.create(0x10003000,9,1024*1024,true,0x70000000);
 require(s.find(c)->stack_begin==sa,"Released stack slot not reusable");
 require(s.select()==c,"Round robin selected sleeper");s.exit(3);require(!s.find(c),"Detached exit leaked task");
 require(s.select()==1,"Main lost after detached exit");s.sleep(10000000);
 require(s.select()==b&&s.time()==1234567,"Idle sleep did not wake earliest task");
 s.exit(0);require(s.select()==1&&s.time()==10000000,"Later sleeper did not resume");
 s.block(T::State::mutex,42);reject([&]{s.select();});s.wake(1);
 reject([&]{s.create(0,0,1024*1024,false,0);});reject([&]{s.create(1,0,0xffffffff,false,0);});
 ds::GuestJumps jumps;std::map<uint32_t,uint32_t> mem;
 cpu.state=main;jumps.save(cpu,4,1,1,[&](auto p,auto v){mem[p]=v;});mem[8]=mem[4];cpu.state={};
 jumps.restore(cpu,8,0,1,1,[&](auto p){return mem[p];});
 require(cpu.Regs()[0]==1&&cpu.Regs()[13]==main.regs[13]&&cpu.ExtRegs()[31]==main.ext[31]&&cpu.Fpscr()==main.fpscr,"Copied jump buffer lost ABI state/zero-return rule");
 reject([&]{jumps.restore(cpu,4,2,2,1,[&](auto p){return mem[p];});});
 reject([&]{jumps.restore(cpu,4,2,1,2,[&](auto p){return mem[p];});});
 jumps.clear_owner(1);reject([&]{jumps.restore(cpu,4,2,1,1,[&](auto p){return mem[p];});});
 {T timed;const auto worker=timed.create(0x10000000,0,65536,false,0x70000000);
  timed.sleep(100);require(timed.select()==worker,"Timed waiter worker not selected");
  timed.block_until(T::State::semaphore,42,50);
  require(timed.select()==worker&&timed.time()==50,"Timed semaphore did not expire before sleeper");
  require(timed.thread().cpu.regs[0]==0xffffffff&&timed.thread().timeout_error==110,"Timed semaphore success fabricated");}
 {T signaled;const auto worker=signaled.create(0x10000000,0,65536,false,0x70000000);
  signaled.select();signaled.block_until(T::State::semaphore,42,50);signaled.select();signaled.wake(worker);signaled.tick(100);
  signaled.select();require(signaled.thread().timeout_error==0&&!signaled.thread().timed_wait,"Canceled timeout fired after signal");}
 std::cout<<"PASS: guest contexts, blocking/wake, timed semaphore expiry/cancellation, sleep, join, TLS and opaque jump guards\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
