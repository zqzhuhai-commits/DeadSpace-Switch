#pragma once
#include "guest_input.hpp"
#include "sdl_audio_output.hpp"
#ifdef __SWITCH__
#include <switch.h>
#endif
namespace ds {
// Basic touch and digital keys; analogue aim/movement and gestures remain.
class SdlGameInput {
    unsigned width,height;
    SDL_GameController* controller=nullptr;
    bool initialized=false;
    bool quit=false;
#ifdef __SWITCH__
    bool panel_touch=false;
    float panel_x=0,panel_y=0;
#endif
    bool confirm_touch=false,trigger_left=false,trigger_right=false;
    float cursor_x=0,cursor_y=0;
    bool menu=true,left_active=false,right_active=false;
    float lx=0,ly=0,rx=0,ry=0,right_x=0,right_y=0;
    int dx=0,dy=0;
    Uint32 last_poll=0;
    static float axis(Sint16 raw,float maximum=1.0f){const float v=float(raw)/32767.0f;return std::fabs(v)<=0.15f?0.0f:std::copysign(std::min((std::fabs(v)-0.15f)/0.85f,maximum),v);}
    void sticks(std::vector<InputEvent>& out){
        const float bx=width*(192.0f/960),by=height*(75.0f/544);
        const float x=bx+width*(75.0f/960)*lx,y=by+height*(75.0f/544)*ly;
        const bool left=lx!=0||ly!=0,right=rx!=0||ry!=0;
        if(left&&!left_active)out.push_back({InputEvent::touch_down,1,0,bx,by});
        if(left)out.push_back({InputEvent::touch_move,1,0,x,y});
        else if(left_active)out.push_back({InputEvent::touch_up,1,0,x,y});
        left_active=left;
        // The game's rear touchpad treats each drag as a finite gesture.
        // Restart it each frame for continuous right-stick camera movement.
        if(right_active)out.push_back({InputEvent::touch_up,2,0,right_x,right_y,1100});
        if(right){
            const float ax=width*(786.0f/960),ay=height*(180.0f/544);
            right_x=ax+width*(155.0f/960)*rx;right_y=ay-height*(105.0f/544)*ry;
            out.push_back({InputEvent::touch_down,2,0,ax,ay,1100});
            out.push_back({InputEvent::touch_move,2,0,right_x,right_y,1100});
        }
        right_active=right;
    }
public:
    SdlGameInput(unsigned w,unsigned h):width(w),height(h),cursor_x(w/2.0f),cursor_y(h/2.0f){
#ifdef __SWITCH__
        hidInitializeTouchScreen();
#endif
        initialized=SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER)==0;if(!initialized)return;for(int i=0;i<SDL_NumJoysticks();++i)if(SDL_IsGameController(i)){controller=SDL_GameControllerOpen(i);if(controller)break;}}
    ~SdlGameInput(){if(controller)SDL_GameControllerClose(controller);}
    bool cursor_visible() const {return menu;}
    float cursor_position_x() const {return cursor_x;}
    float cursor_position_y() const {return cursor_y;}
    bool running() const {return !quit;}
    std::vector<InputEvent> poll(){
        if(!initialized)throw std::runtime_error("SDL controller input initialization failed");
        std::vector<InputEvent> events;SDL_Event event;
#ifdef __SWITCH__
        // Read the panel directly: do not depend on SDL's platform-specific
        // choice between finger and emulated mouse events. HID is 1280x720.
        HidTouchScreenState panel{};
        hidGetTouchScreenStates(&panel,1);
        if(panel.count){
            panel_x=std::clamp(float(panel.touches[0].x)*width/1280.0f,0.0f,float(width-1));
            panel_y=std::clamp(float(panel.touches[0].y)*height/720.0f,0.0f,float(height-1));
            events.push_back({panel_touch?InputEvent::touch_move:InputEvent::touch_down,0,0,panel_x,panel_y});
        }else if(panel_touch)events.push_back({InputEvent::touch_up,0,0,panel_x,panel_y});
        panel_touch=panel.count!=0;
#endif
        while(SDL_PollEvent(&event)){
#ifdef __SWITCH__
            if(event.type==SDL_FINGERDOWN||event.type==SDL_FINGERMOTION||event.type==SDL_FINGERUP||event.type==SDL_MOUSEBUTTONDOWN||event.type==SDL_MOUSEBUTTONUP||event.type==SDL_MOUSEMOTION)continue;
#endif
            if(event.type==SDL_QUIT || (event.type==SDL_WINDOWEVENT&&event.window.event==SDL_WINDOWEVENT_CLOSE)){
                quit=true;continue;
            }
            if(event.type==SDL_CONTROLLERDEVICEADDED&&!controller&&SDL_IsGameController(event.cdevice.which))controller=SDL_GameControllerOpen(event.cdevice.which);
            if(event.type==SDL_CONTROLLERDEVICEREMOVED&&controller&&SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller))==event.cdevice.which){SDL_GameControllerClose(controller);controller=nullptr;lx=ly=rx=ry=0;dx=dy=0;}
            if(events.size()>=54)throw std::runtime_error("SDL input event budget exceeded");
            if(event.type==SDL_FINGERDOWN||event.type==SDL_FINGERMOTION||event.type==SDL_FINGERUP){const auto kind=event.type==SDL_FINGERDOWN?InputEvent::touch_down:event.type==SDL_FINGERMOTION?InputEvent::touch_move:InputEvent::touch_up;events.push_back({kind,0,0,std::clamp(event.tfinger.x*width,0.0f,float(width-1)),std::clamp(event.tfinger.y*height,0.0f,float(height-1))});}
            // Some SDL backends expose panel taps as mouse events. Ignore the
            // synthetic mouse copy when an SDL finger event already exists.
            else if((event.type==SDL_MOUSEBUTTONDOWN||event.type==SDL_MOUSEBUTTONUP)&&event.button.which!=SDL_TOUCH_MOUSEID&&event.button.button==SDL_BUTTON_LEFT){
                cursor_x=std::clamp(float(event.button.x),0.0f,float(width-1));cursor_y=std::clamp(float(event.button.y),0.0f,float(height-1));
                events.push_back({event.type==SDL_MOUSEBUTTONDOWN?InputEvent::touch_down:InputEvent::touch_up,0,0,cursor_x,cursor_y});
            }
            else if(event.type==SDL_MOUSEMOTION&&event.motion.which!=SDL_TOUCH_MOUSEID&&(event.motion.state&SDL_BUTTON_LMASK)){
                cursor_x=std::clamp(float(event.motion.x),0.0f,float(width-1));cursor_y=std::clamp(float(event.motion.y),0.0f,float(height-1));
                events.push_back({InputEvent::touch_move,0,0,cursor_x,cursor_y});
            }
            else if(event.type==SDL_CONTROLLERAXISMOTION){
                switch(event.caxis.axis){
                    case SDL_CONTROLLER_AXIS_LEFTX:lx=axis(event.caxis.value,0.76f);break;
                    case SDL_CONTROLLER_AXIS_LEFTY:ly=axis(event.caxis.value,0.76f);break;
                    case SDL_CONTROLLER_AXIS_RIGHTX:rx=axis(event.caxis.value);break;
                    case SDL_CONTROLLER_AXIS_RIGHTY:ry=axis(event.caxis.value);break;
                    case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
                    case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:{
                        bool& held=event.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERLEFT?trigger_left:trigger_right;
                        const bool down=event.caxis.value>16000;
                        if(held!=down){held=down;events.push_back({down?InputEvent::key_down:InputEvent::key_up,0,event.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERLEFT?102u:103u,0,0});}
                        break;
                    }
                    default:break;
                }
            }
            else if(event.type==SDL_CONTROLLERBUTTONDOWN||event.type==SDL_CONTROLLERBUTTONUP){uint32_t code=0;
                const bool pressed=event.type==SDL_CONTROLLERBUTTONDOWN;
                const auto button=event.cbutton.button;
                if(button==SDL_CONTROLLER_BUTTON_LEFTSTICK||button==SDL_CONTROLLER_BUTTON_RIGHTSTICK){if(pressed){menu=!menu;if(menu)lx=ly=rx=ry=0;}continue;}
                if(button==SDL_CONTROLLER_BUTTON_START&&pressed){menu=true;lx=ly=rx=ry=0;}
                if(menu&&button>=SDL_CONTROLLER_BUTTON_DPAD_UP&&button<=SDL_CONTROLLER_BUTTON_DPAD_RIGHT){
                    if(button==SDL_CONTROLLER_BUTTON_DPAD_LEFT)dx=pressed?-1:(dx<0?0:dx);
                    if(button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT)dx=pressed?1:(dx>0?0:dx);
                    if(button==SDL_CONTROLLER_BUTTON_DPAD_UP)dy=pressed?-1:(dy<0?0:dy);
                    if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)dy=pressed?1:(dy>0?0:dy);
                    continue;
                }
                switch(event.cbutton.button){case SDL_CONTROLLER_BUTTON_B:code=23;break;case SDL_CONTROLLER_BUTTON_A:code=4;break;case SDL_CONTROLLER_BUTTON_X:code=99;break;case SDL_CONTROLLER_BUTTON_Y:code=100;break;case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:code=102;break;case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:code=103;break;case SDL_CONTROLLER_BUTTON_START:code=108;break;case SDL_CONTROLLER_BUTTON_BACK:code=109;break;case SDL_CONTROLLER_BUTTON_DPAD_UP:code=19;break;case SDL_CONTROLLER_BUTTON_DPAD_DOWN:code=20;break;case SDL_CONTROLLER_BUTTON_DPAD_LEFT:code=21;break;case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:code=22;break;default:break;}
                // Xperia Play startup menus need pointer input as well as keys.
                // Keep a press held until release so the guest sees separate frames.
                if(menu&&event.cbutton.button==SDL_CONTROLLER_BUTTON_B){
                    const bool down=event.type==SDL_CONTROLLERBUTTONDOWN;
                    if(down!=confirm_touch){confirm_touch=down;events.push_back({down?InputEvent::touch_down:InputEvent::touch_up,3,0,cursor_x,cursor_y});}
                }
                if(code)events.push_back({event.type==SDL_CONTROLLERBUTTONDOWN?InputEvent::key_down:InputEvent::key_up,0,code,0,0});}
        }
        const Uint32 now=SDL_GetTicks();
        const float elapsed=last_poll?std::min(now-last_poll,Uint32(50))/1000.0f:1.0f/60;
        last_poll=now;
        if(menu){
            cursor_x=std::clamp(cursor_x+dx*width*0.5f*elapsed,0.0f,float(width-1));
            cursor_y=std::clamp(cursor_y+dy*height*0.5f*elapsed,0.0f,float(height-1));

        }
        if(lx!=0||ly!=0||rx!=0||ry!=0){menu=false;dx=dy=0;}
        if(menu&&(left_active||right_active)){lx=ly=rx=ry=0;sticks(events);}
        if(!menu){
            if(confirm_touch){events.push_back({InputEvent::touch_up,3,0,cursor_x,cursor_y});confirm_touch=false;}
            sticks(events);
        }
        return events;
    }
};
}
