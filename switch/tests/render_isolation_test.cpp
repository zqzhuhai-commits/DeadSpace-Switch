#include "guest_gl.hpp"
#include <cassert>
#include <iostream>
#include <cstring>
static unsigned draws=0,finishes=0;
static void draw(unsigned,int,int){++draws;}
static void finish(){++finishes;}
static void active(unsigned){}
static void enable(unsigned){}
static void* resolve(const char* name){
    if(!std::strcmp(name,"glDrawArrays"))return reinterpret_cast<void*>(&draw);
    if(!std::strcmp(name,"glFinish"))return reinterpret_cast<void*>(&finish);
    if(!std::strcmp(name,"glActiveTexture"))return reinterpret_cast<void*>(&active);
    if(!std::strcmp(name,"glEnable"))return reinterpret_cast<void*>(&enable);
    throw std::runtime_error(name);
}
int main(){
    for(unsigned mode=0;mode<3;++mode){
        draws=finishes=0;unsigned argument=0;
        ds::GraphicsBackend backend;backend.resolve=resolve;backend.render_test_mode=mode;
        ds::GlGuestMemory memory;memory.arg=[&](unsigned n){return n==0?argument:0;};
        ds::GuestGl gl(backend,memory);
        // A non-blended draw must survive every mode.
        gl.call("glDrawArrays");assert(draws==1);
        argument=0x0be2;gl.call("glEnable");argument=0;
        gl.call("glDrawArrays");
        assert(draws==(mode==1?1u:2u));
        assert(finishes==(mode==2?2u:0u));
        assert(gl.diagnostic_skipped_draws==(mode==1?1u:0u));
        assert(gl.diagnostic_sync_draws==(mode==2?2u:0u));
    }
    std::cout<<"PASS: baseline preserved; blended omission isolated; synchronous draw counters\n";
}
