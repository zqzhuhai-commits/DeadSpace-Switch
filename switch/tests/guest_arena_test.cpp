#include "guest_arena.hpp"
#include <iostream>
#include <stdexcept>
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
    ds::GuestArena arena;std::vector<std::pair<uint32_t,uint32_t>> freed;
    check(arena.change_break(4096)==0x40000000,"Initial brk failed");
    const auto large=arena.map(0x01000000);check(large==0x4f000000,"Top-down mapping wrong");
    check(arena.contains(*large)&&!arena.contains(*large-1),"Mapping bounds wrong");
    check(arena.unmap(*large,0x01000000,freed)&&freed.size()==1,"Mapping not released");
    check(!arena.contains(*large)&&arena.map(0x01000000)==large,"Released mapping not reusable");
    check(arena.unmap(*large+4096,4096,freed)&&!arena.contains(*large+4096),"Partial unmap failed");
    check(arena.contains(*large)&&arena.contains(*large+8192),"Partial unmap lost adjacent pages");
    const auto middle=arena.map(4096);check(middle==*large+4096,"Hole not reused");
    check(!arena.change_break(0x10000000),"brk collided with mapping");
    check(arena.program_break()==0x40001000,"Failed sbrk changed break");
    check(!arena.unmap(*large+1,4096,freed)&&!arena.map(0),"Invalid map/unmap accepted");
    check(arena.unmap(0x41000000,4096,freed)&&freed.empty(),"Unmapping hole failed");
    check(arena.change_break(-4096)==0x40001000&&!arena.contains(0x40000000),"sbrk shrink failed");
    std::cout<<"PASS: anonymous mapping release/reuse, partial split, sparse holes and brk collision rejection\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
