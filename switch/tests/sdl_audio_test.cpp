// Host-only SDL dummy-driver check. Does not establish audible speaker output.
#include "sdl_audio.hpp"
#include <array>
#include <iostream>
int main(){try{
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    {auto sink=ds::sdl_audio().open(44100,2);sink->playing(true);
    std::array<uint8_t,32768> pcm{};pcm[0]=0xff;pcm[1]=0x7f;
    const auto accepted=sink->write(pcm.data(),pcm.size(),false);
    if(!accepted||accepted>16384||accepted%4)throw std::runtime_error("Unbounded SDL nonblocking queue");
    if(sink->write(pcm.data(),4096,true)!=4096)throw std::runtime_error("SDL blocking write failed");
    sink->playing(false);sink->clear();sink->playing(true);}
    SDL_Quit();std::cout<<"PASS: actual SDL dummy device open, bounded PCM queue, drain/backpressure, pause/clear/close (speakers unverified)\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';SDL_Quit();return 1;}}
