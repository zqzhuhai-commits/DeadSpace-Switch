#include "frame_timing.hpp"
#include "guest_gl.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
static void require(bool condition){if(!condition)throw std::runtime_error("Frame timing regression failed");}
int main(){
    std::vector<std::string> reports;
    ds::GraphicsBackend original;
    original.record=[&](const std::string& text){reports.push_back(text);};
    ds::FrameTiming timing(1000,original.record);
    uint64_t clock=100;
    original.present=[&]{timing.complete(clock-2,clock);clock+=20;};
    auto runtime=std::move(original);
    original.record={}; // Deterministically invalidate the moved-from callback.
    for(unsigned i=0;i<120;++i)runtime.present();
    require(reports.empty());
    runtime.present();
    require(reports.size()==1);
    require(reports[0]=="Frame timing (120): mean_ms=20.00 max_ms=20.00 present_mean_ms=2.00");
    for(unsigned i=0;i<240;++i)runtime.present();
    require(reports.size()==3);
    ds::FrameTiming silent(1000,{});
    for(unsigned i=1;i<500;++i)silent.complete(i*20-2,i*20);
    std::cout<<"PASS: moved graphics backend crosses frames 121/241/361; intervals and empty sink safe\n";
}
