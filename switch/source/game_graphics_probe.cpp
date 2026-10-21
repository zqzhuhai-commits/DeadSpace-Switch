#include "platform_probe.hpp"
#ifdef DS_CPU_PROBE
#include "lifecycle_probe.hpp"
#include "hd_texture.hpp"
#include "app_paths.hpp"
#include "sdl_audio.hpp"
#include <SDL2/SDL.h>
#include <cstdio>

std::string game_graphics_probe(const std::string& root) {
    std::string log="Original-library lifecycle with native GLES1 subset (not verified gameplay)\n";
    if(SDL_Init(SDL_INIT_VIDEO)<0){log+=SDL_GetError();SDL_Quit();return log;}
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
    SDL_Window* window=SDL_CreateWindow("Dead Space Mobile native GL diagnostics",0,0,1280,720,SDL_WINDOW_OPENGL|SDL_WINDOW_SHOWN);
    SDL_GLContext context=window?SDL_GL_CreateContext(window):nullptr;
    if(!context){log+="Native GLES1 context unavailable: "+std::string(SDL_GetError())+"\n";if(window)SDL_DestroyWindow(window);SDL_Quit();return log;}
    auto get_string=reinterpret_cast<const unsigned char*(*)(unsigned)>(SDL_GL_GetProcAddress("glGetString"));
    if(get_string){const auto* renderer=get_string(0x1f01);log+="Renderer: "+std::string(renderer?reinterpret_cast<const char*>(renderer):"unknown")+"\n";}
    ds::GraphicsBackend graphics;
    graphics.resolve=[](const char* name){return SDL_GL_GetProcAddress(name);};
    graphics.present=[&]{SDL_GL_SwapWindow(window);SDL_Event event;while(SDL_PollEvent(&event)){};};
    const std::string hd_root=root==ds::paths::packaged_game_directory?ds::paths::packaged_hd_directory:ds::paths::hd_directory;
    graphics.hd_texture=[hd_root](const std::string& key){return ds::read_hd_texture(hd_root,key);};
    log+="Optional HD root: "+hd_root+" (missing files use original textures)\n";
    log+=lifecycle_probe(root+"/lib/armeabi/libEAMGameDeadSpace.so",root,8,std::move(graphics),ds::sdl_audio());
    auto get_error=reinterpret_cast<unsigned(*)()>(SDL_GL_GetProcAddress("glGetError"));
    if(get_error){char text[80];std::snprintf(text,sizeof(text),"Native context GL error after probe: 0x%x\n",get_error());log+=text;}
    SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();return log;
}
#else
std::string game_graphics_probe(const std::string&) {return "Native game graphics requires the CPU build.\n";}
#endif
