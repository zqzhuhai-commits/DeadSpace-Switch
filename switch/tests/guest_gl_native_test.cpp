// Independent native EGL/desktop GL reference. No Switch or original game claim.
#include "guest_gl.hpp"
#include "hd_texture.hpp"
#include <array>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <stdexcept>

static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F> static void reject(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}require(caught,"Invalid GL operation accepted");}
template<class F> F symbol(void* lib,const char* name){auto p=dlsym(lib,name);if(!p)throw std::runtime_error(name);return reinterpret_cast<F>(p);}
struct Egl {
    void* library=dlopen("libEGL.so.1",RTLD_NOW|RTLD_LOCAL);
    void* gl=dlopen("libGL.so.1",RTLD_NOW|RTLD_LOCAL);
    void* display=nullptr;void* surface=nullptr;void* context=nullptr;
    Egl(){
        require(library&&gl,"Native EGL/GL libraries missing");
        auto get=symbol<void*(*)(const char*)>(library,"eglGetProcAddress");
        auto platform=reinterpret_cast<void*(*)(unsigned,void*,const int*)>(get("eglGetPlatformDisplayEXT"));
        require(platform!=nullptr,"Surfaceless EGL unavailable");display=platform(0x31dd,nullptr,nullptr);
        int major=0,minor=0;require(symbol<unsigned(*)(void*,int*,int*)>(library,"eglInitialize")(display,&major,&minor),"EGL initialize failed");
        require(symbol<unsigned(*)(unsigned)>(library,"eglBindAPI")(0x30a2),"Desktop GL bind failed");
        const int attrs[]={0x3033,1,0x3040,8,0x3024,8,0x3023,8,0x3022,8,0x3021,8,0x3038};
        void* config=nullptr;int count=0;require(symbol<unsigned(*)(void*,const int*,void**,int,int*)>(library,"eglChooseConfig")(display,attrs,&config,1,&count)&&count==1,"EGL config failed");
        const int size[]={0x3057,64,0x3056,64,0x3038};
        surface=symbol<void*(*)(void*,void*,const int*)>(library,"eglCreatePbufferSurface")(display,config,size);
        const int ctx[]={0x3038};context=symbol<void*(*)(void*,void*,void*,const int*)>(library,"eglCreateContext")(display,config,nullptr,ctx);
        require(surface&&context&&symbol<unsigned(*)(void*,void*,void*,void*)>(library,"eglMakeCurrent")(display,surface,surface,context),"EGL compatibility context failed");
    }
    ~Egl(){if(display){symbol<unsigned(*)(void*,void*,void*,void*)>(library,"eglMakeCurrent")(display,nullptr,nullptr,nullptr);if(context)symbol<unsigned(*)(void*,void*)>(library,"eglDestroyContext")(display,context);if(surface)symbol<unsigned(*)(void*,void*)>(library,"eglDestroySurface")(display,surface);symbol<unsigned(*)(void*)>(library,"eglTerminate")(display);}if(gl)dlclose(gl);if(library)dlclose(library);}
    void* resolve(const char* name){auto p=dlsym(gl,name);return p?p:symbol<void*(*)(const char*)>(library,"eglGetProcAddress")(name);}
};
struct Fixture {
    std::vector<uint8_t> memory=std::vector<uint8_t>(1024*1024);
    std::array<uint32_t,16> args{};
    uint32_t next=1024;
    ds::GuestGl bridge;
    Fixture(Egl& egl,std::function<std::vector<uint8_t>(const std::string&)> hd={},uint64_t budget=128*1024*1024):bridge({[&](const char* n){return egl.resolve(n);},{},true,std::move(hd),budget},
        {[&](unsigned n){return args.at(n);},[&](uint32_t n){return allocate(n);},
         [&](uint32_t p,void* out,size_t n){read(p,out,n);},[&](uint32_t p,const void* data,size_t n){write(p,data,n);}}){}
    uint32_t allocate(uint32_t n){if(uint64_t(next)+n+16>memory.size())throw std::runtime_error("Fixture memory exhausted");auto p=next;next+=(n+15)&~15u;return p;}
    void read(uint32_t p,void* out,size_t n){if(!p||uint64_t(p)+n>memory.size())throw std::runtime_error("Guest read outside memory");if(n)std::memcpy(out,memory.data()+p,n);}
    void write(uint32_t p,const void* data,size_t n){if(!p||uint64_t(p)+n>memory.size())throw std::runtime_error("Guest write outside memory");if(n)std::memcpy(memory.data()+p,data,n);}
    template<class T,size_t N> uint32_t upload(const T(&v)[N]){auto p=allocate(sizeof(v));write(p,v,sizeof(v));return p;}
    uint32_t call(const std::string& name,std::initializer_list<uint32_t> words={}){args.fill(0);unsigned i=0;for(auto v:words)args.at(i++)=v;return bridge.call(name);}
    static uint32_t f(float x){uint32_t bits;std::memcpy(&bits,&x,4);return bits;}
    uint32_t word(uint32_t p){uint32_t x;read(p,&x,4);return x;}
    std::string text(uint32_t p){std::string s;while(memory.at(p))s+=char(memory.at(p++));return s;}
    std::array<uint8_t,4> pixel(int x=32,int y=32){const auto p=allocate(8);memory[p+4]=0x7b;call("glReadPixels",{uint32_t(x),uint32_t(y),1,1,0x1908,0x1401,p});require(memory[p+4]==0x7b,"GL readback damaged guard");std::array<uint8_t,4> v{};read(p,v.data(),4);return v;}
    void color(float r,float g,float b){call("glColor4f",{f(r),f(g),f(b),f(1)});}
    void clear(){call("glClearColor",{f(0),f(0),f(0),f(1)});call("glClear",{0x4000});}
};
static std::vector<uint8_t> blue_ktx() {
    const uint8_t magic[]={0xab,0x4b,0x54,0x58,0x20,0x31,0x31,0xbb,0x0d,0x0a,0x1a,0x0a};
    std::vector<uint8_t> out(magic,magic+12);
    const auto word=[&](uint32_t v){for(unsigned i=0;i<4;++i)out.push_back(uint8_t(v>>(8*i)));};
    for(uint32_t v:{0x04030201u,0u,1u,0u,0x87eeu,0x1908u,4u,4u,0u,0u,1u,3u,0u})word(v);
    const uint8_t block[]={255,0,0,0,0,0,0,0,31,0,0,0,0,0,0,0};
    for(unsigned level=0;level<3;++level){word(16);out.insert(out.end(),block,block+16);}
    return out;
}
int main(){
    try {
        Egl egl;std::map<std::string,std::vector<uint8_t>> hd_files;
        const auto load=[&](const std::string& key){auto it=hd_files.find(key);return it==hd_files.end()?std::vector<uint8_t>{}:it->second;};
        Fixture r(egl,load);
        std::cout<<"Native renderer: "<<r.text(r.call("glGetString",{0x1f01}))<<'\n';
        std::cout<<"Native version: "<<r.text(r.call("glGetString",{0x1f02}))<<'\n';
        require(r.text(r.call("glGetString",{0x1f03})).empty(),"Advertised unimplemented extensions");
        r.call("glViewport",{0,0,64,64});r.call("glMatrixMode",{0x1701});r.call("glLoadIdentity");r.call("glMatrixMode",{0x1700});r.call("glLoadIdentity");
        auto query=r.allocate(32);std::fill(r.memory.begin()+query,r.memory.begin()+query+32,0x7b);
        r.call("glGetIntegerv",{0x0ba2,query});require(r.word(query+8)==64&&r.word(query+12)==64&&r.memory[query+16]==0x7b,"Viewport query shape wrong");
        const float identity[]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        const auto matrix=r.upload(identity);r.call("glLoadMatrixf",{matrix});const auto matrix_out=r.allocate(68);r.memory[matrix_out+64]=0x7b;r.call("glGetFloatv",{0x0ba6,matrix_out});require(r.word(matrix_out)==Fixture::f(1)&&r.word(matrix_out+60)==Fixture::f(1)&&r.memory[matrix_out+64]==0x7b,"Matrix query guard failed");
        const float ambient[]={.2f,.2f,.2f,1};r.call("glLightModelfv",{0x0b53,r.upload(ambient)});
        reject([&]{r.call("glColorPointer",{2,0x1401,0,matrix});});
        reject([&]{r.call("glGetIntegerv",{0xdeadbeef,query});});
        r.call("glClearColorx",{32768,16384,65536,65536});r.call("glClear",{0x4000});const auto fp=r.pixel();
        require(fp[0]>=127&&fp[0]<=128&&fp[1]>=63&&fp[1]<=64&&fp[2]==255,"Fixed clear color conversion");
        const int32_t fixed_identity[]={65536,0,0,0,0,65536,0,0,0,0,65536,0,0,0,0,65536};
        r.call("glLoadMatrixx",{r.upload(fixed_identity)});const auto matrix_query=r.allocate(64);
        r.call("glTranslatex",{32768,0,0});r.call("glGetFloatv",{0x0ba6,matrix_query});float translation=0;r.read(matrix_query+48,&translation,4);
        require(translation==.5f,"Fixed matrix translation");r.call("glLoadIdentity");
        r.call("glTexEnvx",{0x2300,0x2200,0x2100});r.call("glAlphaFuncx",{0x0204,32768});
        float alpha=0;reinterpret_cast<void(*)(unsigned,float*)>(egl.resolve("glGetFloatv"))(0x0bc2,&alpha);require(alpha==.5f,"Fixed alpha reference");
        const float triangle[]={9,9,-.8f,-.8f,.8f,-.8f,0,.8f};const auto points=r.upload(triangle);
        r.call("glEnableClientState",{0x8074});r.call("glVertexPointer",{2,0x1406,0,points});r.color(.2f,.8f,1);r.clear();r.call("glDrawArrays",{4,1,3});
        auto pixel=r.pixel();require(pixel[0]>=49&&pixel[0]<=53&&pixel[1]>=202&&pixel[2]>=253,"Client-array triangle framebuffer wrong");
        const auto ids=r.allocate(16);r.call("glGenBuffers",{2,ids});const auto vbo=r.word(ids),ebo=r.word(ids+4);
        r.call("glBindBuffer",{0x8892,vbo});r.call("glBufferData",{0x8892,sizeof(triangle),points,0x88e4});r.call("glVertexPointer",{2,0x1406,0,8});
        const uint16_t index[]={0,1,2};const auto indices=r.upload(index);
        r.call("glBindBuffer",{0x8893,ebo});r.call("glBufferData",{0x8893,sizeof(index),indices,0x88e4});r.clear();r.call("glDrawElements",{4,3,0x1403,0});
        pixel=r.pixel();require(pixel[1]>=202&&pixel[2]>=253,"VBO/EBO offset draw failed");
        const auto before=r.bridge.draws;reject([&]{r.call("glDrawElements",{4,99,0x1403,0});});require(r.bridge.draws==before,"Invalid draw reached native driver");
        r.call("glBufferData",{0x8893,6,0,0x88e4});reject([&]{r.call("glDrawElements",{4,3,0x1403,0});});
        r.call("glBufferSubData",{0x8893,0,sizeof(index),indices});r.call("glDrawElements",{4,3,0x1403,0});
        r.call("glBindBuffer",{0x8892,0});r.call("glBindBuffer",{0x8893,0});
        const float quad[]={-1,-1,1,-1,1,1,-1,-1,1,1,-1,1};const float coords[]={0,0,1,0,1,1,0,0,1,1,0,1};
        const auto quadp=r.upload(quad),uv=r.upload(coords);r.call("glVertexPointer",{2,0x1406,0,quadp});r.call("glTexCoordPointer",{2,0x1406,0,uv});r.call("glEnableClientState",{0x8078});r.call("glEnable",{0x0de1});r.color(1,1,1);
        r.call("glGenTextures",{1,ids});const auto texture=r.word(ids);r.call("glBindTexture",{0x0de1,texture});r.call("glTexParameterx",{0x0de1,0x2801,0x2600});r.call("glTexParameteri",{0x0de1,0x2800,0x2600});r.call("glTexParameteri",{0x0de1,0x2802,0x812f});r.call("glTexParameteri",{0x0de1,0x2803,0x812f});
        const uint8_t rgb[]={255,0,0,0x7a,0,255,0};const auto image=r.upload(rgb);
        r.call("glTexImage2D",{0x0de1,0,0x1907,1,2,0,0x1907,0x1401,image});r.clear();r.call("glDrawArrays",{4,0,6});
        pixel=r.pixel(32,16);require(pixel[0]>250&&pixel[1]<3,"RGB row zero upload wrong");pixel=r.pixel(32,48);require(pixel[0]<3&&pixel[1]>250,"RGB padded row upload wrong");
        r.call("glTexImage2D",{0x0de1,0,0x1908,2,2,0,0x1908,0x1401,0}); // legitimate allocation with null pixels
        const uint8_t blue[]={0,0,255,255,0,0,255,255,0,0,255,255,0,0,255,255};const auto bluep=r.upload(blue);
        r.call("glTexSubImage2D",{0x0de1,0,0,0,2,2,0x1908,0x1401,bluep});r.call("glDrawArrays",{4,0,6});pixel=r.pixel();require(pixel[2]>250&&pixel[0]<3,"Null allocation/texture subimage failed");
        const uint8_t atc[]={255,0,0,0,0,0,0,0,31,0,0,0,0,0,0,0};const auto atcp=r.upload(atc);
        r.call("glCompressedTexImage2D",{0x0de1,0,0x87ee,4,4,0,16,atcp});r.call("glDrawArrays",{4,0,6});pixel=r.pixel();require(pixel[2]>250&&pixel[0]<3&&pixel[3]>250&&r.bridge.atc_uploads==1,"ATC native upload/readback failed");
        reject([&]{r.call("glCompressedTexImage2D",{0x0de1,0,0x87ee,4,4,0,15,atcp});});
        r.call("glGenFramebuffersOES",{1,ids});const auto framebuffer=r.word(ids);r.call("glBindFramebufferOES",{0x8d40,framebuffer});r.call("glFramebufferTexture2DOES",{0x8d40,0x8ce0,0x0de1,texture,0});require(r.call("glCheckFramebufferStatusOES",{0x8d40})==0x8cd5,"Native FBO incomplete");std::fill(r.memory.begin()+query,r.memory.begin()+query+8,0x7b);r.call("glGetIntegerv",{0x8ca6,query});require(r.word(query)==framebuffer&&r.memory[query+4]==0x7b,"FBO binding query/guard incorrect");
        r.call("glBindFramebufferOES",{0x8d40,0});
        const uint8_t red[]={255,0,0,255,255,0,0,255,255,0,0,255,255,0,0,255};
        const auto redp=r.upload(red);const auto redkey=ds::texture_key(2,2,0x1908,std::vector<uint8_t>(red,red+16));hd_files[redkey]=blue_ktx();
        r.call("glGenTextures",{1,ids});r.call("glBindTexture",{0x0de1,r.word(ids)}); // fresh static object; earlier texture was dynamic
        r.call("glTexParameteri",{0x0de1,0x2800,0x2600});
        r.call("glTexParameteri",{0x0de1,0x2801,0x2700});
        r.call("glTexImage2D",{0x0de1,0,0x1908,2,2,0,0x1908,0x1401,redp});
        const uint8_t green[]={0,255,0,255,0,255,0,255,0,255,0,255,0,255,0,255};const auto greenp=r.upload(green);
        r.call("glTexImage2D",{0x0de1,1,0x1908,1,1,0,0x1908,0x1401,greenp}); // later original mip cannot overwrite HD chain
        r.call("glDrawArrays",{4,0,6});pixel=r.pixel();require(pixel[2]>250&&pixel[0]<3&&pixel[1]<3,"Automatic HD substitution not visible");
        require(r.bridge.hd_matches==1&&r.bridge.hd_mip_uploads==3,"HD full mip chain not uploaded");
        r.call("glTexImage2D",{0x0de1,0,0x1908,2,2,0,0x1908,0x1401,redp});require(r.bridge.hd_matches==1&&r.bridge.hd_mip_uploads==3,"Identical base upload needlessly rebuilt HD texture");
        int native_width=0;reinterpret_cast<void(*)(unsigned,int,unsigned,int*)>(egl.resolve("glGetTexLevelParameteriv"))(0x0de1,0,0x1000,&native_width);require(native_width==2,"Draw did not restore original texture binding");
        r.call("glTexSubImage2D",{0x0de1,0,0,0,2,2,0x1908,0x1401,greenp});r.call("glDrawArrays",{4,0,6});pixel=r.pixel();require(pixel[1]>250&&pixel[2]<3&&r.bridge.hd_dynamic_fallbacks==1,"Dynamic update failed to restore original texture");
        r.call("glGenTextures",{1,ids});const auto bad_texture=r.word(ids);r.call("glBindTexture",{0x0de1,bad_texture});r.call("glTexParameteri",{0x0de1,0x2801,0x2600});
        hd_files[redkey]={1,2,3};r.call("glTexImage2D",{0x0de1,0,0x1908,2,2,0,0x1908,0x1401,redp});r.call("glDrawArrays",{4,0,6});pixel=r.pixel();require(pixel[0]>250&&pixel[2]<3&&r.bridge.hd_rejected==1,"Corrupt HD did not fall back to original");
        const uint8_t rgb_red[]={255,0,0,255,0,0,0x7a,0x7b,255,0,0,255,0,0};const auto rgbp=r.upload(rgb_red);
        const std::vector<uint8_t> tight_rgb={255,0,0,255,0,0,255,0,0,255,0,0};hd_files[ds::texture_key(2,2,0x1907,tight_rgb)]=blue_ktx();
        r.call("glGenTextures",{1,ids});r.call("glBindTexture",{0x0de1,r.word(ids)});r.call("glTexParameteri",{0x0de1,0x2801,0x2600});r.call("glTexParameteri",{0x0de1,0x2800,0x2600});
        r.call("glTexImage2D",{0x0de1,0,0x1907,2,2,0,0x1907,0x1401,rgbp});r.call("glTexEnvi",{0x2300,0x2200,0x1e01});r.call("glColor4f",{Fixture::f(1),Fixture::f(1),Fixture::f(1),Fixture::f(.5f)});r.call("glDrawArrays",{4,0,6});pixel=r.pixel();require(pixel[2]>250&&pixel[0]<3&&pixel[3]>=126&&pixel[3]<=129,"HD changed RGB texture alpha semantics or row hashing");r.call("glTexEnvi",{0x2300,0x2200,0x2100});r.color(1,1,1);
        reject([&]{r.call("glCurrentPaletteMatrixOES",{0});});
        r.call("glBindFramebufferOES",{0x8d40,framebuffer});r.call("glFramebufferTexture2DOES",{0x8d40,0x8ce0,0x0de1,r.word(ids),0});r.call("glBindFramebufferOES",{0x8d40,0});r.call("glDrawArrays",{4,0,6});pixel=r.pixel();require(pixel[0]>250&&pixel[2]<3&&r.bridge.hd_dynamic_fallbacks==2,"Render-target attachment did not disable stale HD texture");
        r.call("glVertexPointer",{2,0x1406,0,0xfffffff0});reject([&]{r.call("glDrawArrays",{4,0,6});});
        require(r.call("glGetError")==0,"Native GL reports error");
        hd_files[redkey]=blue_ktx();
        Fixture limited(egl,load,80);const auto q=limited.upload(quad),t=limited.upload(coords),source=limited.upload(red),limited_ids=limited.allocate(4);
        limited.call("glEnableClientState",{0x8074});limited.call("glVertexPointer",{2,0x1406,0,q});limited.call("glEnableClientState",{0x8078});limited.call("glTexCoordPointer",{2,0x1406,0,t});limited.call("glGenTextures",{1,limited_ids});limited.call("glBindTexture",{0x0de1,limited.word(limited_ids)});limited.call("glTexParameteri",{0x0de1,0x2801,0x2600});limited.call("glTexImage2D",{0x0de1,0,0x1908,2,2,0,0x1908,0x1401,source});limited.call("glDrawArrays",{4,0,6});pixel=limited.pixel();require(pixel[0]>250&&pixel[2]<3&&limited.bridge.hd_budget_skips==1&&limited.bridge.hd_matches==0,"HD budget did not preserve original rendering");
        require(limited.call("glGetError")==0,"Native GL error after budget fallback");
        reinterpret_cast<void(*)(unsigned)>(egl.resolve("glEnable"))(0xdeadbeef);
        limited.call("glTexImage2D",{0x0de1,0,0x1908,2,2,0,0x1908,0x1401,source});
        require(limited.call("glGetError")==0x0500&&limited.call("glGetError")==0,"HD probe swallowed original native GL error");
        std::cout<<"PASS: native GL readback, arrays/buffers, pixel bounds, compressed upload, HD/full mips/reuse, original binding, RGB alpha, dynamic/corrupt/budget fallback; draws="<<r.bridge.draws<<'\n';
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
