#include "controller_input.hpp"
#include "gyro_input.hpp"
#include <iostream>
#include <stdexcept>
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
 using C=ds::ControllerInput;using E=ds::GuestInput;using A=ds::GameAction;C c;
 check(c.update({0,.1f,0,.1f,0,true},.016f).empty(),"Drift");
 auto tap=c.update({C::A,0,0,0,0,true},.016f);check(tap.size()==1&&tap[0].kind==E::Kind::pointer&&tap[0].code==0x6000c,"Menu A");
 auto drag=c.update({C::A,1,0,0,0,true},.016f);check(drag.size()==1&&drag[0].kind==E::Kind::pointer&&drag[0].code==0x4000c&&c.cursor_visible(),"Menu stick became game movement");
 c.set_menu(false);auto changed=c.update({C::A,0,0,0,0,true},.016f);check(changed.size()==1&&changed[0].code==0x8000c,"Held A crossed into kinesis");c.update({},.016f);
 const std::pair<uint32_t,A> cases[]={{C::A,A::kinesis},{C::B,A::accept},{C::X,A::turn_stasis},{C::Y,A::reload},{C::R,A::alt_fire},{C::ZR,A::fire},{C::RStick,A::locator},{C::Left,A::previous_weapon},{C::Right,A::next_weapon},{C::Up,A::melee},{C::Down,A::jump}};
 for(auto test:cases){auto e=c.update({test.first,0,0,0,0,true},.016f);check(e.size()==1&&e[0].kind==E::Kind::action&&e[0].code==uint32_t(test.second)&&e[0].down,"NX game action");check(c.update({test.first,0,0,0,0,true},.016f).empty(),"Held action repeats");c.update({},.016f);}
 auto aim=c.update({C::L|C::ZL,0,0,0,0,true},.016f);check(aim.size()==1&&aim[0].code==uint32_t(A::aim)&&aim[0].down,"Combined aim");check(c.update({C::L,0,0,0,0,true},.016f).empty(),"One aim release cleared both");
 auto disconnected=c.update({0,0,0,0,0,false},.016f);check(disconnected.size()==1&&!disconnected[0].down,"Disconnect leaves aim held");
 auto move=c.update({0,1,0,1,0,true},.016f);unsigned movement=0,look=0;for(auto e:move){movement+=e.kind==E::Kind::move;look+=e.kind==E::Kind::look;if(e.kind==E::Kind::look)check(std::fabs(e.x-8.64f)<.001f,"Reduced camera speed");}check(movement==1&&look==1,"Analog move/camera");
 auto held=c.update({0,1,0,1,0,true},.016f);movement=look=0;for(auto e:held){movement+=e.kind==E::Kind::move;look+=e.kind==E::Kind::look;check(e.kind!=E::Kind::pointer,"Analog uses touch D-pad");}check(movement==1&&look==1,"Held analog/camera");
 auto stop=c.update({},.016f);check(stop.size()==1&&stop[0].kind==E::Kind::move&&stop[0].x==0&&stop[0].y==0,"Release leaves movement held");
 for(int angle=0;angle<360;angle+=5){const float theta=angle*3.14159265f/180;float x=.8f*std::cos(theta),y=.8f*std::sin(theta);auto events=c.update({0,x,y,0,0,true},.016f);check(events.size()==1&&events[0].kind==E::Kind::move,"Continuous analog event missing");const auto e=events[0];check(std::fabs(e.x*y-e.y*x)<.00001f&&e.x*x+e.y*y>0,"Movement angle quantized");check(std::fabs(std::hypot(e.x,e.y)-(.8f-.15f)/.85f)<.0001f,"Radial magnitude lost");}
 auto unplug=c.update({0,0,0,0,0,false},.016f);check(unplug.size()==1&&unplug[0].kind==E::Kind::move&&unplug[0].x==0&&unplug[0].y==0,"Disconnect leaves movement held");
 check(c.update({C::Plus,0,0,0,0,true},.016f).empty(),"Pause fired on press");auto pause=c.update({},.016f);check(pause.size()==1&&pause[0].code==uint32_t(A::pause),"Pause on release");
 c.update({C::Plus|C::Minus,0,0,0,0,true},.016f);check(c.update({},.016f).empty(),"Chord exits or pauses");
 c.update({C::LStick,0,0,0,0,true},.016f);check(c.motion_enabled(),"Motion toggle missing");
 ds::GyroInput gyro;auto still=gyro.update(0,0,-1,.001f,0,0,.016f);check(still.first==0&&still.second==0,"Motion tremor not filtered");auto motion=gyro.update(0,0,-1,.1f,0,.2f,.01f);check(std::fabs(motion.first+8.64f)<.001f&&std::fabs(motion.second+4.32f)<.001f,"Motion units or axes");
 std::cout<<"PASS: NX actions, mode boundaries, radial drift, aim pairing, held camera, disconnect, chord isolation and motion conversion\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
