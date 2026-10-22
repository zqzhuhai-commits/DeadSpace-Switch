#pragma once
#include "guest_input.hpp"
#include "sdl_audio_output.hpp"
#include "motion_gestures.hpp"
#include "switch_hid_input.hpp"
#ifdef __SWITCH__
#include <switch.h>
#endif
namespace ds {
// Physical Nintendo controls are normalized by SwitchHidInput before routing.
class SdlGameInput {
    unsigned width,height;
    SDL_GameController* controller=nullptr;
    bool initialized=false;
    bool quit=false;
#ifdef __SWITCH__
    SwitchHidInput native;
    bool panel_touch=false;
    float panel_x=0,panel_y=0;
#endif
    bool confirm_touch=false,trigger_left=false,trigger_right=false;
    std::array<unsigned,256> held_keys{};
    std::array<bool,SDL_CONTROLLER_BUTTON_MAX> buttons{};
    std::array<uint32_t,SDL_CONTROLLER_BUTTON_MAX> button_codes{};
    void key(std::vector<InputEvent>& out,uint32_t code,bool down){
        auto& held=held_keys.at(code);
        if(down){if(held++==0)out.push_back({InputEvent::key_down,0,code});}
        else if(held&&--held==0)out.push_back({InputEvent::key_up,0,code});
    }
    float cursor_x=0,cursor_y=0;
    bool menu=true,cursor_locked=false,left_active=false,right_active=false;
    float lx=0,ly=0,rx=0,ry=0,right_x=0,right_y=0,left_x=0,left_y=0;
    int dx=0,dy=0;
    Uint32 last_poll=0;
    MotionGesture gesture;
    static float axis(Sint16 raw,float maximum=1.0f){const float v=float(raw)/32767.0f;return std::fabs(v)<=0.15f?0.0f:std::copysign(std::min((std::fabs(v)-0.15f)/0.85f,maximum),v);}
    void sticks(std::vector<InputEvent>& out){
        const float bx=width*(192.0f/960),by=height*(75.0f/544);
        const float x=bx+width*(75.0f/960)*lx,y=by+height*(75.0f/544)*ly;
        const bool left=lx!=0||ly!=0,right=rx!=0||ry!=0;
        if(left&&!left_active)out.push_back({InputEvent::touch_down,1,0,bx,by});
        if(left){out.push_back({InputEvent::touch_move,1,0,x,y});left_x=x;left_y=y;}
        else if(left_active)out.push_back({InputEvent::touch_up,1,0,left_x,left_y});
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
    std::string status() const {return std::string(menu?"cursor":"gameplay")+" L="+std::to_string(lx)+","+std::to_string(ly)+" R="+std::to_string(rx)+","+std::to_string(ry);}
    std::vector<InputEvent> poll(){
        if(!initialized)throw std::runtime_error("SDL controller input initialization failed");
        std::vector<InputEvent> events;SDL_Event event;
        std::vector<SDL_Event> pending_events;
#ifdef __SWITCH__
        // Read the panel directly: do not depend on SDL's platform-specific
        // choice between finger and emulated mouse events. HID is 1280x720.
        pending_events=native.poll();
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
            if(SwitchHidInput::duplicate(event))continue;
            if(event.type==SDL_FINGERDOWN||event.type==SDL_FINGERMOTION||event.type==SDL_FINGERUP||event.type==SDL_MOUSEBUTTONDOWN||event.type==SDL_MOUSEBUTTONUP||event.type==SDL_MOUSEMOTION)continue;
#endif
            pending_events.push_back(event);
        }
        for(const auto& event:pending_events){
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
                        if(held!=down){
                            held=down;
                            key(events,event.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERLEFT?102u:103u,down);
                        }
                        break;
                    }
                    default:break;
                }
            }
            else if(event.type==SDL_CONTROLLERBUTTONDOWN||event.type==SDL_CONTROLLERBUTTONUP){
                const bool pressed=event.type==SDL_CONTROLLERBUTTONDOWN;
                const auto button=event.cbutton.button;
                if(button>=buttons.size()||buttons[button]==pressed)continue;
                buttons[button]=pressed;
                if(!pressed&&button_codes[button]){
                    key(events,button_codes[button],false);button_codes[button]=0;
                }
                if(button==SDL_CONTROLLER_BUTTON_LEFTSTICK){if(pressed)gesture.start(false,SDL_GetTicks());continue;}
                if(!menu&&button==SDL_CONTROLLER_BUTTON_DPAD_DOWN){if(pressed)gesture.start(true,SDL_GetTicks());continue;}
                // User mapping: L3 rotates the weapon; R3 toggles the menu cursor.
                if(button==SDL_CONTROLLER_BUTTON_RIGHTSTICK){if(pressed){menu=!menu;cursor_locked=menu;dx=dy=0;}continue;}
                if(button==SDL_CONTROLLER_BUTTON_START&&pressed){menu=true;cursor_locked=true;dx=dy=0;}
                if(menu&&button>=SDL_CONTROLLER_BUTTON_DPAD_UP&&button<=SDL_CONTROLLER_BUTTON_DPAD_RIGHT){
                    if(button==SDL_CONTROLLER_BUTTON_DPAD_LEFT)dx=pressed?-1:(dx<0?0:dx);
                    if(button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT)dx=pressed?1:(dx>0?0:dx);
                    if(button==SDL_CONTROLLER_BUTTON_DPAD_UP)dy=pressed?-1:(dy<0?0:dy);
                    if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)dy=pressed?1:(dy>0?0:dy);
                    continue;
                }
                uint32_t code=0;
                // SDL names here use the existing positional convention:
                // Nintendo A=B, B=A, X=Y, Y=X. Native HID guarantees it.
                switch(button){
                    case SDL_CONTROLLER_BUTTON_B:code=23;break; // A: interact
                    case SDL_CONTROLLER_BUTTON_A:code=4;break;  // B: kinesis
                    case SDL_CONTROLLER_BUTTON_Y:code=20;break; // X: saw (donor D-pad down)
                    case SDL_CONTROLLER_BUTTON_X:code=100;break;// Y: quick turn
                    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:code=100;break;// L: aimed stasis
                    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:code=99;break;// R: reload
                    case SDL_CONTROLLER_BUTTON_START:code=108;break;
                    case SDL_CONTROLLER_BUTTON_BACK:code=109;break;
                    case SDL_CONTROLLER_BUTTON_DPAD_UP:code=19;break;
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:code=21;break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:code=22;break;
                    default:break;
                }
                if(menu&&button==SDL_CONTROLLER_BUTTON_B&&pressed!=confirm_touch){
                    confirm_touch=pressed;
                    events.push_back({pressed?InputEvent::touch_down:InputEvent::touch_up,3,0,cursor_x,cursor_y});
                }
                if(pressed&&code){button_codes[button]=code;key(events,code,true);}
            }
        }
        const Uint32 now=SDL_GetTicks();
        const float elapsed=last_poll?std::min(now-last_poll,Uint32(50))/1000.0f:1.0f/60;
        last_poll=now;
        const auto previous_cursor_x=cursor_x,previous_cursor_y=cursor_y;
        if(menu){
            cursor_x=std::clamp(cursor_x+(dx+rx)*width*0.5f*elapsed,0.0f,float(width-1));
            cursor_y=std::clamp(cursor_y+(dy+ry)*height*0.5f*elapsed,0.0f,float(height-1));

        }
        if(menu&&!cursor_locked&&(std::fabs(lx)>0.5f||std::fabs(ly)>0.5f)){menu=false;dx=dy=0;}
        if(menu&&(left_active||right_active)){
            const auto slx=lx,sly=ly,srx=rx,sry=ry;
            lx=ly=rx=ry=0;sticks(events);
            lx=slx;ly=sly;rx=srx;ry=sry;
        }
        if(menu&&confirm_touch&&(cursor_x!=previous_cursor_x||cursor_y!=previous_cursor_y))events.push_back({InputEvent::touch_move,3,0,cursor_x,cursor_y});
        if(!menu){
            if(confirm_touch){events.push_back({InputEvent::touch_up,3,0,cursor_x,cursor_y});confirm_touch=false;}
            sticks(events);
        }
        const bool synthetic_motion=gesture.active();
        gesture.poll(now,events);
#ifdef __SWITCH__
        if(!synthetic_motion)native.motion(events);
#else
        (void)synthetic_motion;
#endif
        return events;
    }
};
}
