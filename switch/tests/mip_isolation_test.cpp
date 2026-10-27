#include "guest_gl.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <stdexcept>
static unsigned unit=0;
static std::array<int,2> filters{0x2703,0x2700};
static bool throwing=false;
static unsigned queries=0;
static void active(unsigned n){unit=n-0x84c0;assert(unit<2);}
static void bind(unsigned,unsigned){}
static void enable(unsigned){}
static void delete_textures(int,const unsigned*){}
static void get_integer(unsigned,int* out){*out=2;}
static void get_filter(unsigned,unsigned,int* out){++queries;*out=filters[unit];}
static void set_filter(unsigned,unsigned,int value){filters[unit]=value;}
static void draw(unsigned,int,int){
    assert(unit==1);assert(filters[0]==0x2601&&filters[1]==0x2601);
    if(throwing)throw std::runtime_error("draw failed");
}
static void* resolve(const char* name){
#define MAP(n,f) if(!std::strcmp(name,n))return reinterpret_cast<void*>(&f)
    MAP("glActiveTexture",active);MAP("glBindTexture",bind);MAP("glEnable",enable);
    MAP("glGetIntegerv",get_integer);MAP("glGetTexParameteriv",get_filter);
    MAP("glTexParameteri",set_filter);MAP("glDrawArrays",draw);
    MAP("glDeleteTextures",delete_textures);
#undef MAP
    throw std::runtime_error(name);
}
int main(){
    for(bool production:{false,true})for(bool fail:{false,true}){
        throwing=fail;queries=0;std::array<unsigned,4> args{};
        ds::GraphicsBackend backend;backend.resolve=resolve;backend.render_test_mode=production?0:3;backend.base_mip_only=production;
        ds::GlGuestMemory memory;memory.arg=[&](unsigned n){return args.at(n);};
        memory.read=[](unsigned p,void* out,size_t n){assert(p==32&&n==4);const unsigned id=11;std::memcpy(out,&id,4);};
        ds::GuestGl gl(backend,memory);
        for(unsigned u=0;u<2;++u){
            args={0x84c0+u,0,0,0};gl.call("glActiveTexture");
            args={0x0de1,u+10,0,0};gl.call("glBindTexture");gl.call("glEnable");
        }
        args={4,0,0,0};bool caught=false;
        try{gl.call("glDrawArrays");}catch(const std::runtime_error&){caught=true;}
        assert(caught==fail);assert(unit==1);
        assert(filters[0]==0x2703&&filters[1]==0x2700);
        assert(queries==2);
        try{gl.call("glDrawArrays");}catch(const std::runtime_error&){}
        assert(queries==2); // Unchanged textures require no new driver query.
        args={0x0de1,0x2801,0x2700,0};gl.call("glTexParameteri");
        args={4,0,0,0};try{gl.call("glDrawArrays");}catch(const std::runtime_error&){}
        assert(queries==3); // Guest setter invalidates exactly the bound object.
        assert(unit==1&&filters[0]==0x2703&&filters[1]==0x2700);
        args={1,32,0,0};gl.call("glDeleteTextures");
        args={0x0de1,11,0,0};gl.call("glBindTexture");
        args={4,0,0,0};try{gl.call("glDrawArrays");}catch(const std::runtime_error&){}
        assert(queries==4); // Reused texture names must not retain a deleted object's cache.
    }
    std::cout<<"PASS: mip filters isolated on two units; original filters and active unit restored on success/failure\n";
}

