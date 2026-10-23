#include "sdl_game_input.hpp"
#include "menu_cursor.hpp"
#include <iostream>
#include <stdexcept>
static void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
static void push(SDL_Event& event){check(SDL_PushEvent(&event)==1,"Cannot push test input");}
int main(){
    try {
        {
            ds::SdlGameInput input(1280,720);
            check(input.running(),"Session starts stopped");
            SDL_Event event{};event.type=SDL_FINGERDOWN;event.tfinger.x=0.5f;event.tfinger.y=0.5f;push(event);
            auto events=input.poll();
            check(events.size()==1&&events[0].kind==ds::InputEvent::touch_down&&events[0].x==640&&events[0].y==360,"Touch does not reach the game");
            event={};event.type=SDL_CONTROLLERBUTTONDOWN;event.cbutton.button=SDL_CONTROLLER_BUTTON_START;push(event);
            events=input.poll();
            check(input.running()&&events.size()==1&&events[0].kind==ds::InputEvent::key_down&&events[0].code==108,"Start must reach guest, not exit");
            event={};event.type=SDL_CONTROLLERBUTTONDOWN;event.cbutton.button=SDL_CONTROLLER_BUTTON_B;push(event);
            events=input.poll();check(events.size()==2&&events[0].kind==ds::InputEvent::touch_down&&events[0].x==640&&events[0].y==360,"Confirm must press touchscreen");
            check(input.poll().empty(),"Confirm release must wait for button up");
            event.type=SDL_CONTROLLERBUTTONUP;push(event);events=input.poll();
            check(events.size()==2&&events[0].kind==ds::InputEvent::touch_up,"Confirm must release touchscreen");
            event={};event.type=SDL_MOUSEBUTTONDOWN;event.button.button=SDL_BUTTON_LEFT;event.button.x=200;event.button.y=100;push(event);
            events=input.poll();check(events.size()==1&&events[0].x==200&&events[0].y==100,"Mouse panel fallback failed");
            event.button.which=SDL_TOUCH_MOUSEID;push(event);check(input.poll().empty(),"Touch mouse duplicate must be ignored");
            event={};event.type=SDL_CONTROLLERAXISMOTION;event.caxis.axis=SDL_CONTROLLER_AXIS_LEFTX;event.caxis.value=32767;push(event);
            events=input.poll();check(!input.cursor_visible()&&events.size()==2&&events[0].kind==ds::InputEvent::touch_down&&events[1].kind==ds::InputEvent::touch_move&&events[1].id==1,"Left stick must drag movement surface");
            event.caxis.value=0;push(event);events=input.poll();check(events.size()==1&&events[0].kind==ds::InputEvent::touch_up,"Left stick must release at neutral");
            event.caxis.axis=SDL_CONTROLLER_AXIS_RIGHTX;event.caxis.value=32767;push(event);events=input.poll();
            check(events.size()==2&&events[0].module==1100&&ds::pointer_arguments(events[1],1280,720)[1]==1100,"Right stick must use rear touchpad ABI");
            events=input.poll();check(events.size()==3&&events[0].kind==ds::InputEvent::touch_up&&events[1].kind==ds::InputEvent::touch_down,"Held right stick must restart camera gesture");
            event.caxis.value=0;push(event);events=input.poll();check(events.size()==1&&events[0].kind==ds::InputEvent::touch_up,"Right stick must release at neutral");
            event.caxis.axis=SDL_CONTROLLER_AXIS_TRIGGERLEFT;event.caxis.value=32767;push(event);events=input.poll();
            check(events.size()==1&&events[0].code==102&&events[0].kind==ds::InputEvent::key_down,"Left trigger mapping");
            SDL_Event shoulder{};shoulder.type=SDL_CONTROLLERBUTTONDOWN;shoulder.cbutton.button=SDL_CONTROLLER_BUTTON_LEFTSHOULDER;push(shoulder);
            check(input.poll().empty(),"Shoulder must not duplicate held trigger key");
            event.caxis.value=0;push(event);check(input.poll().empty(),"Trigger release must retain held shoulder");
            shoulder.type=SDL_CONTROLLERBUTTONUP;push(shoulder);events=input.poll();check(events.size()==1&&events[0].kind==ds::InputEvent::key_up,"Last shared input must release key");
            event={};event.type=SDL_CONTROLLERBUTTONDOWN;event.cbutton.button=SDL_CONTROLLER_BUTTON_START;push(event);input.poll();check(input.cursor_visible()&&input.running(),"Pause restores menu cursor without exiting");

        }
        {
            ds::SdlGameInput input(1280,720);SDL_Event event{};event.type=SDL_QUIT;push(event);input.poll();
            check(!input.running(),"Window close did not stop session");
        }
        SDL_Quit();std::cout<<"PASS: SDL touch coordinates, Start forwarding, confirm touch pairing, mouse fallback and window-close cancellation\n";
    }catch(const std::exception& e){SDL_Quit();std::cerr<<e.what()<<'\n';return 1;}
}
