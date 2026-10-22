#include "sdl_game_input.hpp"
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
            check(input.poll().empty()&&!input.running(),"Exit button forwarded to guest or ignored");
        }
        {
            ds::SdlGameInput input(1280,720);SDL_Event event{};event.type=SDL_QUIT;push(event);input.poll();
            check(!input.running(),"Window close did not stop session");
        }
        SDL_Quit();std::cout<<"PASS: SDL touch coordinates, exit-button interception and window-close cancellation\n";
    }catch(const std::exception& e){SDL_Quit();std::cerr<<e.what()<<'\n';return 1;}
}
