#pragma once
// Linux host reference; Switch uses its SDL GLES context.
#include <dlfcn.h>
#include <stdexcept>
inline void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F> F symbol(void* lib,const char* name){auto p=dlsym(lib,name);if(!p)throw std::runtime_error(name);return reinterpret_cast<F>(p);}
struct HostEgl {
    void* library=dlopen("libEGL.so.1",RTLD_NOW|RTLD_LOCAL);
    void* gl=dlopen("libGL.so.1",RTLD_NOW|RTLD_LOCAL);
    void* display=nullptr;void* surface=nullptr;void* context=nullptr;
    HostEgl(){
        require(library&&gl,"Native EGL/GL libraries missing");
        auto get=symbol<void*(*)(const char*)>(library,"eglGetProcAddress");
        auto platform=reinterpret_cast<void*(*)(unsigned,void*,const int*)>(get("eglGetPlatformDisplayEXT"));
        require(platform!=nullptr,"Surfaceless EGL unavailable");display=platform(0x31dd,nullptr,nullptr);
        int major=0,minor=0;require(symbol<unsigned(*)(void*,int*,int*)>(library,"eglInitialize")(display,&major,&minor),"EGL initialize failed");
        require(symbol<unsigned(*)(unsigned)>(library,"eglBindAPI")(0x30a2),"Desktop GL bind failed");
        const int attrs[]={0x3033,1,0x3040,8,0x3024,8,0x3023,8,0x3022,8,0x3021,8,0x3038};
        void* config=nullptr;int count=0;require(symbol<unsigned(*)(void*,const int*,void**,int,int*)>(library,"eglChooseConfig")(display,attrs,&config,1,&count)&&count==1,"EGL config failed");
        const int size[]={0x3057,1280,0x3056,720,0x3038};
        surface=symbol<void*(*)(void*,void*,const int*)>(library,"eglCreatePbufferSurface")(display,config,size);
        const int ctx[]={0x3038};context=symbol<void*(*)(void*,void*,void*,const int*)>(library,"eglCreateContext")(display,config,nullptr,ctx);
        require(surface&&context&&symbol<unsigned(*)(void*,void*,void*,void*)>(library,"eglMakeCurrent")(display,surface,surface,context),"EGL compatibility context failed");
    }
    ~HostEgl(){if(display){symbol<unsigned(*)(void*,void*,void*,void*)>(library,"eglMakeCurrent")(display,nullptr,nullptr,nullptr);if(context)symbol<unsigned(*)(void*,void*)>(library,"eglDestroyContext")(display,context);if(surface)symbol<unsigned(*)(void*,void*)>(library,"eglDestroySurface")(display,surface);symbol<unsigned(*)(void*)>(library,"eglTerminate")(display);}if(gl)dlclose(gl);if(library)dlclose(library);}
    void* resolve(const char* name){auto p=dlsym(gl,name);return p?p:symbol<void*(*)(const char*)>(library,"eglGetProcAddress")(name);}
};
