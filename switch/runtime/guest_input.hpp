#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <cmath>
namespace ds {
struct InputEvent {enum Kind {touch_down,touch_move,touch_up,key_down,key_up} kind;uint32_t id=0,code=0;float x=0,y=0;uint32_t module=1000;};
inline std::array<uint32_t,5> pointer_arguments(const InputEvent& event,uint32_t width,uint32_t height){
    if(event.kind!=InputEvent::touch_down&&event.kind!=InputEvent::touch_move&&event.kind!=InputEvent::touch_up)throw std::runtime_error("Invalid pointer event kind");
    if(event.id>=10||!std::isfinite(event.x)||!std::isfinite(event.y)||event.x<0||event.y<0||event.x>=width||event.y>=height)throw std::runtime_error("Invalid pointer coordinates/id");
    uint32_t x,y;std::memcpy(&x,&event.x,4);std::memcpy(&y,&event.y,4);
    if(event.module!=1000&&event.module!=1100)throw std::runtime_error("Invalid pointer module");
    return {event.kind==InputEvent::touch_down?0x6000cu:event.kind==InputEvent::touch_move?0x4000cu:0x8000cu,event.module,event.id,x,y};
}
}
