#include "controller_input.hpp"
#include <iostream>
#include <stdexcept>
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
    using C=ds::ControllerInput;using E=ds::GuestInput;C mapper;
    check(mapper.update({0,.1f,0,.1f,0,true},.016f).empty(),"Stick drift emitted input");
    auto click=mapper.update({C::A,0,0,0,0,true},.016f);check(click.size()==1&&click[0].kind==E::Kind::pointer&&click[0].code==0x6000c,"Menu A did not tap");
    auto release=mapper.update({},.016f);check(release.size()==1&&release[0].code==0x8000c,"Menu A release lost");
    auto aim=mapper.update({C::ZL,1,0,1,0,true},.016f);bool key=false,move=false,camera=false;
    for(auto e:aim){key|=e.kind==E::Kind::key&&e.code==102&&e.down;move|=e.kind==E::Kind::pointer&&e.id==1;camera|=e.kind==E::Kind::pointer&&e.id==2;}
    check(key&&move&&camera&&!mapper.cursor_visible(),"Gameplay aim/movement/camera missing");
    auto held=mapper.update({C::ZL,1,0,1,0,true},.016f);int down=0,up=0;for(auto e:held)if(e.kind==E::Kind::pointer&&e.id==2){down+=e.code==0x6000c;up+=e.code==0x8000c;}check(down==1&&up==1,"Held camera did not continue swiping");
    auto disconnect=mapper.update({0,0,0,0,0,false},.016f);key=false;up=0;for(auto e:disconnect){key|=e.kind==E::Kind::key&&e.code==102&&!e.down;up+=e.kind==E::Kind::pointer&&e.code==0x8000c;}
    check(key&&up==2,"Disconnect left input stuck");
    auto gesture=mapper.update({C::Up,0,0,0,0,true},.016f);unsigned samples=0;for(auto e:gesture)samples+=e.kind==E::Kind::acceleration;
    check(samples>0,"Weapon mode gesture was not emitted");
    for(unsigned i=0;i<30;++i)for(auto e:mapper.update({C::Up,0,0,0,0,true},.016f))samples+=e.kind==E::Kind::acceleration;
    check(samples==sizeof(ds::kWeaponTilt)/sizeof(ds::kWeaponTilt[0]),"Holding mode button restarted or truncated gesture");
    auto quit=mapper.update({C::Plus|C::Minus,0,0,0,0,true},.016f);check(quit.size()==1&&quit[0].kind==E::Kind::quit,"Quit chord failed");
    std::cout<<"PASS: menu/game context, dead zone, held camera gestures, aim and disconnect releases\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
