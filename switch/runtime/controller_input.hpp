#pragma once
#include "guest_input.hpp"
#include "accel_gestures.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

namespace ds {
class ControllerInput {
public:
    enum Button:uint32_t {A=1u,B=2u,X=4u,Y=8u,L=16u,R=32u,ZL=64u,ZR=128u,Plus=256u,Minus=512u,Up=1024u,Down=2048u,Left=4096u,Right=8192u,RStick=16384u};
    struct Snapshot {uint32_t buttons=0;float lx=0,ly=0,rx=0,ry=0;bool connected=true;};
private:
    uint32_t previous=0;
    std::set<uint32_t> keys;
    bool left_drag=false,right_drag=false,accept_pointer=false,accept_key=false,cursor=true;
    float cursor_x=320,cursor_y=240,right_x=0,right_y=0;
    const AccelSample* gesture=nullptr;size_t gesture_count=0,gesture_index=0;float gesture_elapsed=0,gesture_next=0;
    static float axis(float v,float cap){v=std::clamp(v,-1.0f,1.0f);const auto magnitude=std::fabs(v);if(magnitude<=0.15f)return 0;return std::copysign(std::min(cap,(magnitude-0.15f)/0.85f),v);}
public:
    bool cursor_visible()const{return cursor;}
    float x()const{return cursor_x;}float y()const{return cursor_y;}
    std::vector<GuestInput> update(Snapshot state,float seconds) {
        std::vector<GuestInput> out;
        if(!state.connected)state=Snapshot{0,0,0,0,0,false};
        const uint32_t pressed=state.buttons&~previous;
        auto pointer=[&](uint32_t code,uint32_t module,uint32_t id,float x,float y){GuestInput e;e.kind=GuestInput::Kind::pointer;e.code=code;e.module=module;e.id=id;e.x=x;e.y=y;out.push_back(e);};
        if((state.buttons&(Plus|Minus))==(Plus|Minus)){GuestInput e;e.kind=GuestInput::Kind::quit;out.push_back(e);return out;}
        const float lx=axis(state.lx,.76f),ly=axis(state.ly,.76f),rx=axis(state.rx,1),ry=axis(state.ry,1);
        if(pressed&RStick)cursor=!cursor;
        if(pressed&Plus)cursor=true;
        if(std::max({std::fabs(lx),std::fabs(ly),std::fabs(rx),std::fabs(ry)})>.35f)cursor=false;
        if(!state.connected)cursor=false;
        seconds=std::clamp(seconds,0.0f,.1f);
        if(!state.connected)gesture=nullptr;
        if(!cursor&&(pressed&(Up|Down))){
            gesture=(pressed&Up)?kWeaponTilt:kZeroGJump;
            gesture_count=(pressed&Up)?sizeof(kWeaponTilt)/sizeof(kWeaponTilt[0]):sizeof(kZeroGJump)/sizeof(kZeroGJump[0]);gesture_index=0;gesture_elapsed=0;gesture_next=0;
        }
        gesture_elapsed+=seconds*1000;
        while(gesture&&gesture_index<gesture_count&&gesture_elapsed>=gesture_next){const auto& sample=gesture[gesture_index++];GuestInput e;e.kind=GuestInput::Kind::acceleration;e.x=sample.x;e.y=sample.y;e.z=sample.z;out.push_back(e);gesture_next+=sample.delay_ms;if(gesture_index==gesture_count)gesture=nullptr;}
        if(cursor){cursor_x=std::clamp(cursor_x+float(bool(state.buttons&Right)-bool(state.buttons&Left))*220*seconds,0.0f,639.0f);cursor_y=std::clamp(cursor_y+float(bool(state.buttons&Down)-bool(state.buttons&Up))*220*seconds,0.0f,479.0f);}
        if(accept_pointer&&(!(state.buttons&A)||!cursor)){pointer(0x8000c,1000,3,cursor_x,cursor_y);accept_pointer=false;}
        if((pressed&A)&&cursor){pointer(0x6000c,1000,3,cursor_x,cursor_y);accept_pointer=true;}
        else if(accept_pointer)pointer(0x4000c,1000,3,cursor_x,cursor_y);
        if(pressed&A)accept_key=!cursor;
        if(!(state.buttons&A))accept_key=false;
        std::set<uint32_t> desired;
        const std::pair<uint32_t,uint32_t> mapping[]={{B,4},{X,99},{Y,100},{L,21},{R,22},{ZL,102},{ZR,103},{Plus,108},{Minus,109}};
        for(const auto& item:mapping)if(state.buttons&item.first)desired.insert(item.second);
        if(accept_key&&(state.buttons&A))desired.insert(23);
        for(auto code:keys)if(!desired.count(code)){GuestInput e;e.code=code;e.down=false;out.push_back(e);}
        for(auto code:desired)if(!keys.count(code)){GuestInput e;e.code=code;e.down=true;out.push_back(e);}
        keys=std::move(desired);
        const bool left=!cursor&&(lx!=0||ly!=0),right=!cursor&&(rx!=0||ry!=0);
        const float lpx=128+640*(75.0f/960)*lx,lpy=480*(75.0f/544)+480*(75.0f/544)*ly;
        if(left&&!left_drag)pointer(0x6000c,1000,1,128,480*(75.0f/544));
        if(left)pointer(0x4000c,1000,1,lpx,lpy);else if(left_drag)pointer(0x8000c,1000,1,lpx,lpy);
        if(right_drag)pointer(0x8000c,1100,2,right_x,right_y);
        if(right){pointer(0x6000c,1100,2,640*(786.0f/960),480*(180.0f/544));right_x=640*(786.0f/960)+640*(155.0f/960)*rx;right_y=480*(180.0f/544)-480*(105.0f/544)*ry;pointer(0x4000c,1100,2,right_x,right_y);}
        left_drag=left;right_drag=right;previous=state.buttons;
        return out;
    }
};
}
