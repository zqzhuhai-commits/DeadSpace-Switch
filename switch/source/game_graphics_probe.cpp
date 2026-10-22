#include "platform_probe.hpp"
#ifdef DS_CPU_PROBE
#include "lifecycle_probe.hpp"
#include "hd_texture.hpp"
#include "app_paths.hpp"
#include "boot_log.hpp"
#include "sdl_audio_output.hpp"
#include "sdl_game_input.hpp"
#include <memory>
#include <SDL2/SDL.h>
#include <cstdio>
#include <switch.h>
#include <sys/stat.h>
#include <cerrno>
#include <chrono>

std::string game_graphics_probe(const std::string& root,bool interactive) {
    auto checkpoint=[](const std::string& message){ds::boot_log(ds::paths::log_file,message);};
    checkpoint("BEGIN: game_graphics_probe");
    if(mkdir(ds::paths::save_directory,0777)&&errno!=EEXIST)return "BLOCKED: cannot create writable save directory.\n";
    std::string log="Original-library lifecycle with native GLES1 subset (not verified gameplay)\n";
    checkpoint("BEGIN: SDL video init");
    if(SDL_Init(SDL_INIT_VIDEO)<0){log+=SDL_GetError();SDL_Quit();return log;}
    checkpoint("END: SDL video init OK");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
    checkpoint("BEGIN: SDL_CreateWindow GLES 1.1");
    SDL_Window* window=SDL_CreateWindow("Dead Space Mobile native GL diagnostics",0,0,1280,720,SDL_WINDOW_OPENGL|SDL_WINDOW_SHOWN);
    checkpoint(window?"END: SDL_CreateWindow OK; BEGIN: SDL_GL_CreateContext":"ERROR: SDL_CreateWindow failed");
    SDL_GLContext context=window?SDL_GL_CreateContext(window):nullptr;
    if(!context){log+="Native GLES1 context unavailable: "+std::string(SDL_GetError())+"\n";if(window)SDL_DestroyWindow(window);SDL_Quit();return log;}
    checkpoint("END: SDL_GL_CreateContext OK");
    SDL_GL_SetSwapInterval(1);
    auto get_string=reinterpret_cast<const unsigned char*(*)(unsigned)>(SDL_GL_GetProcAddress("glGetString"));
    if(get_string){const auto* renderer=get_string(0x1f01);log+="Renderer: "+std::string(renderer?reinterpret_cast<const char*>(renderer):"unknown")+"\n";}
    if(get_string)for(const unsigned name:{0x1f00u,0x1f01u,0x1f02u}) {
        const auto* value=get_string(name);
        checkpoint("GL STRING "+std::to_string(name)+": "+(value?reinterpret_cast<const char*>(value):"unavailable"));
    }
    // Test rendering/readback/presentation before guest code changes GL state.
    auto clear_color=reinterpret_cast<void(*)(float,float,float,float)>(SDL_GL_GetProcAddress("glClearColor"));
    auto clear=reinterpret_cast<void(*)(unsigned)>(SDL_GL_GetProcAddress("glClear"));
    auto read=reinterpret_cast<void(*)(int,int,int,int,unsigned,unsigned,void*)>(SDL_GL_GetProcAddress("glReadPixels"));
    auto error=reinterpret_cast<unsigned(*)()>(SDL_GL_GetProcAddress("glGetError"));
    if(!clear_color||!clear||!read||!error) {
        SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();
        return "BLOCKED: required basic GL entries missing.\n";
    }
    checkpoint("BEGIN: blue GL readback/present test");
    const auto previous_error=error();
    clear_color(0.125f,0.25f,0.5f,1.0f);clear(0x4000);
    unsigned char pixel[4]={0,0,0,0};read(640,360,1,1,0x1908,0x1401,pixel);
    const auto test_error=error();
    checkpoint("GL BLUE TEST: rgba="+std::to_string(pixel[0])+","+std::to_string(pixel[1])+","+std::to_string(pixel[2])+","+std::to_string(pixel[3])+" error="+std::to_string(test_error)+" previous_error="+std::to_string(previous_error));
    SDL_GL_SwapWindow(window);SDL_Delay(250);
    clear_color(0,0,0,0);
    checkpoint("END: blue GL readback/present test");
    ds::GraphicsBackend graphics;
    graphics.progress=checkpoint;
    graphics.monotonic_ns=[] {return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());};
    graphics.width=1280;graphics.height=720;
    graphics.resolve=[](const char* name){return SDL_GL_GetProcAddress(name);};
    unsigned presented=0;
    graphics.present=[&]{
        if(!presented)checkpoint("BEGIN: first SDL_GL_SwapWindow");
        SDL_GL_SwapWindow(window);
        if(!presented++)checkpoint("END: first SDL_GL_SwapWindow");
    };
    auto input=std::make_unique<ds::SdlGameInput>(1280,720);
    graphics.input=[&](unsigned){return input->poll();};
    graphics.keep_running=[&]{return appletMainLoop()&&input->running();};
    const std::string hd_root=root==ds::paths::packaged_game_directory?ds::paths::packaged_hd_directory:ds::paths::hd_directory;
    graphics.hd_texture=[hd_root](const std::string& key){return ds::read_hd_texture(hd_root,key);};
    log+="Optional HD root: "+hd_root+" (missing files use original textures)\n";
    checkpoint(std::string("JIT environment: CodeMemory hints=")+(envIsSyscallHinted(0x4b)&&envIsSyscallHinted(0x4c)?"yes; range cache synchronization":"no; legacy permission transitions"));
    checkpoint("BEGIN: audio backend construction");
    ds::SdlAudioOutput audio;
    checkpoint("END: audio backend construction; BEGIN: guest runtime");
    log+=lifecycle_probe(root+"/lib/armeabi/libEAMGameDeadSpace.so",root,interactive?0:8,std::move(graphics),audio.backend(),ds::paths::save_directory);
    audio.close();input.reset();
    auto get_error=reinterpret_cast<unsigned(*)()>(SDL_GL_GetProcAddress("glGetError"));
    if(get_error){char text[80];std::snprintf(text,sizeof(text),"Native context GL error after probe: 0x%x\n",get_error());log+=text;}
    SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();return log;
}
#else
std::string game_graphics_probe(const std::string&,bool) {return "Native game graphics requires the CPU build.\n";}
#endif
