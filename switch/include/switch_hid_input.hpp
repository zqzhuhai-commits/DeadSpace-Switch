#pragma once
#ifdef __SWITCH__
#include <switch.h>
#include <algorithm>
#include <vector>
#include <SDL2/SDL.h>
#include "guest_input.hpp"
namespace ds {
// Read Nintendo's physical labels directly, independent of SDL's configurable
// Nintendo/Xbox face-button naming and controller database.
class SwitchHidInput {
    PadState pad{};
    HidSixAxisSensorHandle sensors[4]{};
    bool ready[4]{};
    uint64_t last_sample[4]{};
    static constexpr Sint32 source=-777;
    static void button(Uint8 button,bool down){
        SDL_Event e{};e.type=down?SDL_CONTROLLERBUTTONDOWN:SDL_CONTROLLERBUTTONUP;
        e.cbutton.which=source;e.cbutton.button=button;SDL_PushEvent(&e);
    }
    static void axis(Uint8 axis,Sint16 value){
        SDL_Event e{};e.type=SDL_CONTROLLERAXISMOTION;e.caxis.which=source;
        e.caxis.axis=axis;e.caxis.value=value;SDL_PushEvent(&e);
    }
public:
    SwitchHidInput(){
        padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
        const auto open=[&](unsigned first,int n,HidNpadIdType id,HidNpadStyleTag style){
            if(R_FAILED(hidGetSixAxisSensorHandles(sensors+first,n,id,style)))return;
            for(int j=0;j<n;++j)ready[first+j]=R_SUCCEEDED(hidStartSixAxisSensor(sensors[first+j]));
        };
        open(0,1,HidNpadIdType_Handheld,HidNpadStyleTag_NpadHandheld);
        open(1,1,HidNpadIdType_No1,HidNpadStyleTag_NpadFullKey);
        open(2,2,HidNpadIdType_No1,HidNpadStyleTag_NpadJoyDual);
    }
    ~SwitchHidInput(){for(unsigned i=0;i<4;++i)if(ready[i])hidStopSixAxisSensor(sensors[i]);}
    static bool duplicate(const SDL_Event& e){
        if(e.type==SDL_CONTROLLERDEVICEADDED||e.type==SDL_CONTROLLERDEVICEREMOVED)return true;
        if(e.type==SDL_CONTROLLERAXISMOTION)return e.caxis.which!=source;
        if(e.type==SDL_CONTROLLERBUTTONDOWN||e.type==SDL_CONTROLLERBUTTONUP)return e.cbutton.which!=source;
        return false;
    }
    void poll(){
        padUpdate(&pad);
        const uint64_t down=padGetButtonsDown(&pad),up=padGetButtonsUp(&pad);
        struct Mapping {uint64_t hid;Uint8 sdl;};
        const Mapping map[]={
            {HidNpadButton_A,SDL_CONTROLLER_BUTTON_B},{HidNpadButton_B,SDL_CONTROLLER_BUTTON_A},
            {HidNpadButton_X,SDL_CONTROLLER_BUTTON_Y},{HidNpadButton_Y,SDL_CONTROLLER_BUTTON_X},
            {HidNpadButton_L,SDL_CONTROLLER_BUTTON_LEFTSHOULDER},{HidNpadButton_R,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER},
            {HidNpadButton_Plus,SDL_CONTROLLER_BUTTON_START},{HidNpadButton_Minus,SDL_CONTROLLER_BUTTON_BACK},
            {HidNpadButton_StickL,SDL_CONTROLLER_BUTTON_LEFTSTICK},{HidNpadButton_StickR,SDL_CONTROLLER_BUTTON_RIGHTSTICK},
            {HidNpadButton_Up,SDL_CONTROLLER_BUTTON_DPAD_UP},{HidNpadButton_Down,SDL_CONTROLLER_BUTTON_DPAD_DOWN},
            {HidNpadButton_Left,SDL_CONTROLLER_BUTTON_DPAD_LEFT},{HidNpadButton_Right,SDL_CONTROLLER_BUTTON_DPAD_RIGHT}};
        for(const auto& m:map){if(down&m.hid)button(m.sdl,true);if(up&m.hid)button(m.sdl,false);}
        const auto l=padGetStickPos(&pad,0),r=padGetStickPos(&pad,1);
        axis(SDL_CONTROLLER_AXIS_LEFTX,Sint16(l.x));axis(SDL_CONTROLLER_AXIS_LEFTY,Sint16(std::clamp(-l.y,-32767,32767)));
        axis(SDL_CONTROLLER_AXIS_RIGHTX,Sint16(r.x));axis(SDL_CONTROLLER_AXIS_RIGHTY,Sint16(std::clamp(-r.y,-32767,32767)));
        const auto held=padGetButtons(&pad);
        axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,(held&HidNpadButton_ZL)?32767:0);
        axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT,(held&HidNpadButton_ZR)?32767:0);
    }
    void motion(std::vector<InputEvent>& events){
        // The original mobile game consumes acceleration, not angular velocity.
        // Select the active pad's sensor so disconnected handles add no samples.
        const auto style=padGetStyleSet(&pad);
        const unsigned i=(style&HidNpadStyleTag_NpadHandheld)?0:(style&HidNpadStyleTag_NpadFullKey)?1:2;
        HidSixAxisSensorState state{};
        if(ready[i]&&hidGetSixAxisSensorStates(sensors[i],&state,1)>0&&state.sampling_number&&state.sampling_number!=last_sample[i]){
            last_sample[i]=state.sampling_number;
            events.push_back({InputEvent::acceleration,0,0,state.acceleration.x*9.80665f,
                              state.acceleration.y*9.80665f,1000,state.acceleration.z*9.80665f});
        }
    }
};
}
#endif
