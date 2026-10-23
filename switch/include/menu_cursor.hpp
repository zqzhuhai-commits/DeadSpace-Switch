#pragma once
#include "sdl_game_input.hpp"
namespace ds {
// Paint after guest rendering, restoring every GL state changed by the cursor.
inline void draw_menu_cursor(const SdlGameInput& input,int width,int height){
    if(!input.cursor_visible()||width<=0||height<=0)return;
    auto resolve=[](const char* name){return SDL_GL_GetProcAddress(name);};
    const auto bind=reinterpret_cast<void(*)(unsigned,unsigned)>(resolve("glBindFramebufferOES"));
    const auto integer=reinterpret_cast<void(*)(unsigned,int*)>(resolve("glGetIntegerv"));
    const auto floating=reinterpret_cast<void(*)(unsigned,float*)>(resolve("glGetFloatv"));
    const auto boolean=reinterpret_cast<void(*)(unsigned,unsigned char*)>(resolve("glGetBooleanv"));
    const auto enabled=reinterpret_cast<unsigned char(*)(unsigned)>(resolve("glIsEnabled"));
    const auto enable=reinterpret_cast<void(*)(unsigned)>(resolve("glEnable"));
    const auto disable=reinterpret_cast<void(*)(unsigned)>(resolve("glDisable"));
    const auto mask=reinterpret_cast<void(*)(unsigned char,unsigned char,unsigned char,unsigned char)>(resolve("glColorMask"));
    const auto scissor=reinterpret_cast<void(*)(int,int,int,int)>(resolve("glScissor"));
    const auto color=reinterpret_cast<void(*)(float,float,float,float)>(resolve("glClearColor"));
    const auto clear=reinterpret_cast<void(*)(unsigned)>(resolve("glClear"));
    if(!bind||!integer||!floating||!boolean||!enabled||!enable||!disable||!mask||!scissor||!color||!clear)return;
    int fb=0,box[4]{};float previous[4]{};unsigned char channels[4]{};
    integer(0x8ca6,&fb);integer(0x0c10,box);floating(0x0c22,previous);boolean(0x0c23,channels);
    const bool was_scissor=enabled(0x0c11);
    bind(0x8d40,0);enable(0x0c11);mask(1,1,1,1);
    const int x=int(input.cursor_position_x()*width/1280.0f),y=height-1-int(input.cursor_position_y()*height/720.0f);
    const int scale=std::max(1,width/640);
    auto rect=[&](int ox,int oy,int w,int h){
        const int x0=std::max(0,x+ox*scale),y0=std::max(0,y+oy*scale);
        const int x1=std::min(width,x+(ox+w)*scale),y1=std::min(height,y+(oy+h)*scale);
        if(x1>x0&&y1>y0){scissor(x0,y0,x1-x0,y1-y0);clear(0x4000);}
    };
    color(0,0,0,1);rect(-6,-2,13,5);rect(-2,-6,5,13);
    color(0.82f,0.96f,1,1);rect(-5,-1,11,3);rect(-1,-5,3,11);
    color(previous[0],previous[1],previous[2],previous[3]);mask(channels[0],channels[1],channels[2],channels[3]);
    scissor(box[0],box[1],box[2],box[3]);if(!was_scissor)disable(0x0c11);bind(0x8d40,unsigned(fb));
}
}
