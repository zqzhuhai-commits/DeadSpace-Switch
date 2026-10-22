#pragma once
#include "guest_input.hpp"
#include "sdl_audio_output.hpp"
namespace ds {
// Basic touch and digital keys; analogue aim/movement and gestures remain.
class SdlGameInput {
    unsigned width,height;
    SDL_GameController* controller=nullptr;
    bool initialized=false;
    bool quit=false;
public:
    SdlGameInput(unsigned w,unsigned h):width(w),height(h){initialized=SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER)==0;if(!initialized)return;for(int i=0;i<SDL_NumJoysticks();++i)if(SDL_IsGameController(i)){controller=SDL_GameControllerOpen(i);if(controller)break;}}
    ~SdlGameInput(){if(controller)SDL_GameControllerClose(controller);}
    bool running() const {return !quit;}
    std::vector<InputEvent> poll(){
        if(!initialized)throw std::runtime_error("SDL controller input initialization failed");
        std::vector<InputEvent> events;SDL_Event event;
        while(SDL_PollEvent(&event)){
            if(event.type==SDL_QUIT || (event.type==SDL_WINDOWEVENT&&event.window.event==SDL_WINDOWEVENT_CLOSE) ||
               (event.type==SDL_CONTROLLERBUTTONDOWN&&event.cbutton.button==SDL_CONTROLLER_BUTTON_START)){
                quit=true;continue;
            }
            if(events.size()>=64)throw std::runtime_error("SDL input event budget exceeded");
            if(event.type==SDL_FINGERDOWN||event.type==SDL_FINGERMOTION||event.type==SDL_FINGERUP){const auto kind=event.type==SDL_FINGERDOWN?InputEvent::touch_down:event.type==SDL_FINGERMOTION?InputEvent::touch_move:InputEvent::touch_up;events.push_back({kind,0,0,std::clamp(event.tfinger.x*width,0.0f,float(width-1)),std::clamp(event.tfinger.y*height,0.0f,float(height-1))});}
            else if(event.type==SDL_CONTROLLERBUTTONDOWN||event.type==SDL_CONTROLLERBUTTONUP){uint32_t code=0;
                switch(event.cbutton.button){case SDL_CONTROLLER_BUTTON_B:code=23;break;case SDL_CONTROLLER_BUTTON_A:code=4;break;case SDL_CONTROLLER_BUTTON_X:code=99;break;case SDL_CONTROLLER_BUTTON_Y:code=100;break;case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:code=102;break;case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:code=103;break;case SDL_CONTROLLER_BUTTON_START:code=108;break;case SDL_CONTROLLER_BUTTON_BACK:code=109;break;case SDL_CONTROLLER_BUTTON_DPAD_UP:code=19;break;case SDL_CONTROLLER_BUTTON_DPAD_DOWN:code=20;break;case SDL_CONTROLLER_BUTTON_DPAD_LEFT:code=21;break;case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:code=22;break;default:break;}
                if(code)events.push_back({event.type==SDL_CONTROLLERBUTTONDOWN?InputEvent::key_down:InputEvent::key_up,0,code,0,0});}
        }
        return events;
    }
};
}
