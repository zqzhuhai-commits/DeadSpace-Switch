#pragma once
#include "guest_input.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
namespace ds {
// Layout and menu/game separation follow hazevauks/deadspace_nx (MIT).
// Engine entry points are independently checked for the Xperia Play donor.
class ControllerInput {
public:
    enum Button:uint32_t {A=1u,B=2u,X=4u,Y=8u,L=16u,R=32u,ZL=64u,ZR=128u,Plus=256u,Minus=512u,Up=1024u,Down=2048u,Left=4096u,Right=8192u,RStick=16384u,LStick=32768u};
    struct Snapshot {uint32_t buttons=0;float lx=0,ly=0,rx=0,ry=0;bool connected=true;};
private:
    bool menu=true,was_menu=true,left_down=false,click_down=false,back_down=false,aim_down=false,combo=false,motion=false;
    uint32_t previous=0;
    float cursor_x=320,cursor_y=240,last_x=128,last_y=480*75.0f/544,idle=0;
    static void axes(float& x,float& y){const float length=std::hypot(x,y);if(length<=.15f){x=y=0;return;}const float k=std::min(1.0f,(length-.15f)/.85f)/length;x*=k;y*=k;}
public:
    void set_menu(bool value){menu=value;}
    bool is_menu()const{return menu;}
    bool motion_enabled()const{return motion;}
    bool cursor_visible()const{return menu&&idle<6;}
    float x()const{return cursor_x;}float y()const{return cursor_y;}
    std::vector<GuestInput> update(Snapshot state,float seconds){
        std::vector<GuestInput> out;
        if(!state.connected)state=Snapshot{0,0,0,0,0,false};
        seconds=std::clamp(seconds,.001f,.05f);
        auto action=[&](GameAction code,bool down=true){GuestInput e;e.kind=GuestInput::Kind::action;e.code=uint32_t(code);e.down=down;out.push_back(e);};
        auto pointer=[&](uint32_t code,uint32_t id,float x,float y){GuestInput e;e.kind=GuestInput::Kind::pointer;e.module=1000;e.code=code;e.id=id;e.x=x;e.y=y;out.push_back(e);};
        auto release=[&]{if(left_down)pointer(0x8000c,1,last_x,last_y);if(click_down)pointer(0x8000c,2,cursor_x,cursor_y);if(back_down){GuestInput e;e.code=4;e.down=false;out.push_back(e);}if(aim_down)action(GameAction::aim,false);left_down=click_down=back_down=aim_down=false;};
        if(menu!=was_menu||!state.connected){release();previous=state.buttons;was_menu=menu;}
        if(!state.connected){previous=0;return out;}
        const uint32_t pressed=state.buttons&~previous,released=previous&~state.buttons;
        float lx=state.lx,ly=state.ly,rx=state.rx,ry=state.ry;axes(lx,ly);axes(rx,ry);
        if(pressed&LStick)motion=!motion;
        if((state.buttons&(Plus|Minus))==(Plus|Minus))combo=true;
        if(released&(Plus|Minus)){
            if(!combo){if(released&Plus)action(GameAction::pause);else if(!menu)action(GameAction::rig);}
            if(!(state.buttons&(Plus|Minus)))combo=false;
        }
        if(menu){
            float dx=(lx+rx)*450*seconds,dy=(ly+ry)*450*seconds;
            if(pressed&Left)dx-=12;
            if(pressed&Right)dx+=12;
            if(pressed&Up)dy-=12;
            if(pressed&Down)dy+=12;
            cursor_x=std::clamp(cursor_x+dx,0.0f,639.0f);cursor_y=std::clamp(cursor_y+dy,0.0f,479.0f);
            idle=(dx||dy||(state.buttons&A))?0:idle+seconds;
            if(pressed&A){pointer(0x6000c,2,cursor_x,cursor_y);click_down=true;}
            else if(click_down&&(dx||dy))pointer(0x4000c,2,cursor_x,cursor_y);
            if((released&A)&&click_down){pointer(0x8000c,2,cursor_x,cursor_y);click_down=false;}
            const bool back=state.buttons&B;if(back!=back_down){GuestInput e;e.code=4;e.down=back;out.push_back(e);back_down=back;}
        }else{
            const bool aiming=state.buttons&(L|ZL);if(aiming!=aim_down){action(GameAction::aim,aiming);aim_down=aiming;}
            const std::pair<uint32_t,GameAction> map[]={{ZR,GameAction::fire},{R,GameAction::alt_fire},{B,GameAction::accept},{A,GameAction::kinesis},{Y,GameAction::reload},{X,GameAction::turn_stasis},{RStick,GameAction::jump},{Left,GameAction::previous_weapon},{Right,GameAction::next_weapon},{Up,GameAction::melee},{Down,GameAction::locator}};
            for(const auto& binding:map)if(pressed&binding.first)action(binding.second);
            if(lx||ly){if(!left_down)pointer(0x6000c,1,128,480*75.0f/544);last_x=(192+75*lx)*640/960;last_y=(75+75*ly)*480/544;pointer(0x4000c,1,last_x,last_y);left_down=true;}
            else if(left_down){pointer(0x8000c,1,last_x,last_y);left_down=false;}
            if(rx||ry){GuestInput e;e.kind=GuestInput::Kind::look;e.x=rx*900*seconds;e.y=ry*600*seconds;out.push_back(e);}
        }
        previous=state.buttons;return out;
    }
};
}
