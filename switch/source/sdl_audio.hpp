#pragma once
#include "guest_audio.hpp"
#include <SDL2/SDL.h>
#include <algorithm>

namespace ds {
class SdlAudioSink final:public AudioSink {
    SDL_AudioDeviceID device=0;
    uint32_t capacity=0,frame_bytes=0;
public:
    SdlAudioSink(uint32_t rate,uint32_t channels){
        if(SDL_InitSubSystem(SDL_INIT_AUDIO)<0)throw std::runtime_error(std::string("SDL audio init: ")+SDL_GetError());
        SDL_AudioSpec want{},got{};want.freq=int(rate);want.channels=uint8_t(channels);want.format=AUDIO_S16LSB;want.samples=1024;
        device=SDL_OpenAudioDevice(nullptr,0,&want,&got,0);
        if(!device)throw std::runtime_error(std::string("SDL audio open: ")+SDL_GetError());
        frame_bytes=channels*2;capacity=4096*frame_bytes;
    }
    ~SdlAudioSink()override{if(device)SDL_CloseAudioDevice(device);}
    void playing(bool on)override{SDL_PauseAudioDevice(device,on?0:1);}
    void clear()override{SDL_ClearQueuedAudio(device);}
    uint32_t write(const uint8_t* pcm,uint32_t size,bool blocking)override{
        const uint32_t n=std::min(size,capacity);const uint32_t start=SDL_GetTicks();
        while(blocking&&SDL_GetQueuedAudioSize(device)>capacity-n){
            if(SDL_GetTicks()-start>2000)throw std::runtime_error("SDL audio queue did not drain within 2 seconds");
            SDL_Delay(1);
        }
        const auto queued=SDL_GetQueuedAudioSize(device);
        const auto accepted=blocking?n:std::min(n,capacity-std::min(capacity,queued))/frame_bytes*frame_bytes;
        if(accepted&&SDL_QueueAudio(device,pcm,accepted)<0)throw std::runtime_error(std::string("SDL PCM queue: ")+SDL_GetError());
        return accepted;
    }
};
inline AudioBackend sdl_audio(){return {"SDL PCM16 output",[](uint32_t hz,uint32_t ch){return std::make_unique<SdlAudioSink>(hz,ch);}};}
}
