#include "motion_gestures.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
static void check(bool v,const char* text){if(!v)throw std::runtime_error(text);}
int main(){
    ds::MotionGesture gesture;
    std::vector<ds::InputEvent> events;
    gesture.start(false,100);
    gesture.poll(100,events);
    check(events.size()==1,"Weapon tilt must begin on press");
    auto words=ds::acceleration_arguments(events[0]);float x=0;std::memcpy(&x,&words[0],4);
    check(x==ds::kWeaponTilt[0].x,"Softfp acceleration ABI corrupted");
    gesture.start(true,101); // Do not interrupt an in-flight weapon gesture.
    gesture.poll(101,events);check(events.size()==1,"Cadence collapsed");
    gesture.poll(5000,events);check(events.size()==2,"Low FPS must emit one sample per poll");
    check(events.back().z==ds::kWeaponTilt[1].z,"Repeated press replaced gesture");
    for(unsigned i=2;i<sizeof(ds::kWeaponTilt)/sizeof(*ds::kWeaponTilt);++i)gesture.poll(5000+i*100,events);
    check(!gesture.active(),"Weapon gesture never completed");
    check(events.size()==sizeof(ds::kWeaponTilt)/sizeof(*ds::kWeaponTilt),"Weapon sample lost");
    events.clear();gesture.start(true,0xfffffff0u);gesture.poll(0xfffffff0u,events);
    gesture.poll(5,events);check(events.size()==1,"Tick wrap triggered sample too soon");
    gesture.poll(10,events);check(events.size()==2,"Tick wrap lost zero-G sample");
    auto bad=events[0];bad.x=std::numeric_limits<float>::infinity();
    try{ds::acceleration_arguments(bad);throw std::logic_error("Nonfinite motion accepted");}
    catch(const std::runtime_error&){}
    std::cout<<"PASS: motion softfp ABI, independent gestures, cadence, low FPS and timer wrap\n";
}
