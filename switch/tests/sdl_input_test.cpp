#include "sdl_game_input.hpp"
#include "menu_cursor.hpp"
#include <iostream>
#include <stdexcept>
static void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
static void push(SDL_Event& event){check(SDL_PushEvent(&event)==1,"Cannot push test input");}
static void button(Uint8 b,bool down){SDL_Event e{};e.type=down?SDL_CONTROLLERBUTTONDOWN:SDL_CONTROLLERBUTTONUP;e.cbutton.button=b;push(e);}
static void axis(Uint8 a,Sint16 v){SDL_Event e{};e.type=SDL_CONTROLLERAXISMOTION;e.caxis.axis=a;e.caxis.value=v;push(e);}
static bool key(const std::vector<ds::InputEvent>& e,uint32_t code,bool down){return std::any_of(e.begin(),e.end(),[&](const auto& v){return v.kind==(down?ds::InputEvent::key_down:ds::InputEvent::key_up)&&v.code==code;});}
int main(){
    try {
        {
            ds::SdlGameInput input(1280,720);
            SDL_Event event{};event.type=SDL_FINGERDOWN;event.tfinger.x=0.5f;event.tfinger.y=0.5f;push(event);
            auto events=input.poll();check(events.size()==1&&events[0].x==640&&events[0].y==360,"Touch scaling");
            button(SDL_CONTROLLER_BUTTON_START,true);events=input.poll();check(input.running()&&key(events,108,true),"Start must reach guest, not exit");
            button(SDL_CONTROLLER_BUTTON_START,false);input.poll();
            button(SDL_CONTROLLER_BUTTON_B,true);events=input.poll();
            check(events.size()==2&&events[0].kind==ds::InputEvent::touch_down,"A must click menu");
            check(input.poll().empty(),"Confirm must stay held");
            button(SDL_CONTROLLER_BUTTON_B,false);events=input.poll();check(events.size()==2&&events.back().kind==ds::InputEvent::touch_up,"Confirm pairing");
            event={};event.type=SDL_MOUSEBUTTONDOWN;event.button.button=SDL_BUTTON_LEFT;event.button.x=200;event.button.y=100;push(event);
            events=input.poll();check(events.size()==1&&events[0].x==200&&events[0].y==100,"Mouse fallback");
            event.button.which=SDL_TOUCH_MOUSEID;push(event);check(input.poll().empty(),"Duplicate mouse touch");
            button(SDL_CONTROLLER_BUTTON_RIGHTSTICK,true);input.poll();
            button(SDL_CONTROLLER_BUTTON_RIGHTSTICK,false);input.poll();
            // R3 explicitly leaves cursor mode; sustained sticks now drive gameplay.
            axis(SDL_CONTROLLER_AXIS_LEFTX,8000);events=input.poll();check(!input.cursor_visible(),"Gameplay mode must remain stable");
            axis(SDL_CONTROLLER_AXIS_LEFTX,0);input.poll();
            axis(SDL_CONTROLLER_AXIS_LEFTX,32767);events=input.poll();check(!input.cursor_visible()&&events.size()==2&&events[0].kind==ds::InputEvent::touch_down,"Left movement");
            axis(SDL_CONTROLLER_AXIS_LEFTX,0);events=input.poll();check(events.size()==1&&events[0].kind==ds::InputEvent::touch_up,"Movement release");
            axis(SDL_CONTROLLER_AXIS_RIGHTX,32767);events=input.poll();check(events.size()==2&&events[0].module==1100,"Rear touchpad ABI");
            events=input.poll();check(events.size()==3&&events[0].kind==ds::InputEvent::touch_up,"Continuous camera");
            axis(SDL_CONTROLLER_AXIS_RIGHTX,0);input.poll();
            axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,32767);check(key(input.poll(),102,true),"ZL must aim");
            button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER,true);check(key(input.poll(),100,true),"L must send aimed stasis independently of ZL");
            button(SDL_CONTROLLER_BUTTON_X,true);check(input.poll().empty(),"Y and L share donor action without duplicate presses");
            button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER,false);check(input.poll().empty(),"Y must retain shared action");
            button(SDL_CONTROLLER_BUTTON_X,false);check(key(input.poll(),100,false),"Final shared action release");
            axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,0);check(key(input.poll(),102,false),"Aim release");
            const std::pair<Uint8,uint32_t> mappings[]={{SDL_CONTROLLER_BUTTON_B,23},{SDL_CONTROLLER_BUTTON_A,4},{SDL_CONTROLLER_BUTTON_Y,20},{SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,99},{SDL_CONTROLLER_BUTTON_DPAD_UP,19},{SDL_CONTROLLER_BUTTON_DPAD_LEFT,21},{SDL_CONTROLLER_BUTTON_DPAD_RIGHT,22},{SDL_CONTROLLER_BUTTON_BACK,109}};
            for(auto [b,c]:mappings){button(b,true);check(key(input.poll(),c,true),"Gameplay mapping");button(b,false);check(key(input.poll(),c,false),"Gameplay release");}
            button(SDL_CONTROLLER_BUTTON_LEFTSTICK,true);events=input.poll();check(!input.cursor_visible()&&events.size()==1&&events[0].kind==ds::InputEvent::acceleration,"L3 must rotate weapon, not toggle cursor");
            button(SDL_CONTROLLER_BUTTON_LEFTSTICK,false);input.poll();
            button(SDL_CONTROLLER_BUTTON_RIGHTSTICK,true);input.poll();check(input.cursor_visible(),"R3 must recover cursor");
            button(SDL_CONTROLLER_BUTTON_RIGHTSTICK,false);input.poll();
        }
        {
            ds::SdlGameInput input(1280,720);
            axis(SDL_CONTROLLER_AXIS_RIGHTX,32767);
            auto events=input.poll();
            check(input.cursor_visible()&&input.cursor_position_x()>640,"Right stick must move boot/menu cursor without hiding it");
            check(std::none_of(events.begin(),events.end(),[](auto& e){return e.module==1100;}),"Cursor leaked rear touchpad input");
            button(SDL_CONTROLLER_BUTTON_START,true);input.poll();button(SDL_CONTROLLER_BUTTON_START,false);input.poll();
            axis(SDL_CONTROLLER_AXIS_LEFTX,32767);input.poll();
            check(input.cursor_visible(),"Explicit pause cursor must stay visible with held sticks");
            button(SDL_CONTROLLER_BUTTON_RIGHTSTICK,true);events=input.poll();
            check(!input.cursor_visible(),"R3 must enter gameplay even when sticks are held");
            check(std::any_of(events.begin(),events.end(),[](auto& e){return e.id==1&&e.kind==ds::InputEvent::touch_down;}),"Held left stick did not restart after cursor toggle");
            button(SDL_CONTROLLER_BUTTON_RIGHTSTICK,false);input.poll();
            axis(SDL_CONTROLLER_AXIS_LEFTX,0);events=input.poll();
            check(std::any_of(events.begin(),events.end(),[](auto& e){return e.id==1&&e.kind==ds::InputEvent::touch_up&&e.x>256;}),"Left release must use last movement coordinates");
        }
        {
            ds::SdlGameInput input(1280,720);SDL_Event event{};event.type=SDL_QUIT;push(event);input.poll();check(!input.running(),"Window close");
        }
        SDL_Quit();std::cout<<"PASS: touch, dual sticks, Nintendo actions, independent ZL/L, shared key release, L3 weapon tilt and R3 cursor\n";
    }catch(const std::exception& e){SDL_Quit();std::cerr<<e.what()<<'\n';return 1;}
}
