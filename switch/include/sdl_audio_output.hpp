#pragma once
#include "guest_audio.hpp"
#include "pcm_buffer_gate.hpp"
#ifdef __SWITCH__
#include <SDL2/SDL.h>
#else
#include <SDL.h>
#endif
namespace ds {
class SdlAudioOutput {
    SDL_AudioDeviceID device=0;
    PcmBufferGate gate;
    bool paused=true;
    void update(uint32_t queued){
        const bool next=gate.paused(queued,SDL_GetTicks());
        if(next!=paused){SDL_PauseAudioDevice(device,next?1:0);paused=next;}
    }
public:
    SdlAudioOutput()=default;
    SdlAudioOutput(const SdlAudioOutput&)=delete;
    SdlAudioOutput& operator=(const SdlAudioOutput&)=delete;
    ~SdlAudioOutput(){close();}
    void close(){if(device){SDL_ClearQueuedAudio(device);SDL_CloseAudioDevice(device);device=0;}}
    void tick(){if(device)update(SDL_GetQueuedAudioSize(device));}
    std::string report() const {return "SDL audio: underruns="+std::to_string(gate.underruns)+" starts="+std::to_string(gate.starts)+"; 100ms prebuffer, 250ms queue cap.\n";}
    AudioBackend backend(){return {
        [this](unsigned rate,unsigned channels){
            if(device)throw std::runtime_error("SDL PCM output already open");
            if(SDL_InitSubSystem(SDL_INIT_AUDIO))throw std::runtime_error(SDL_GetError());
            SDL_AudioSpec wanted{},actual{};wanted.freq=int(rate);wanted.format=AUDIO_S16LSB;wanted.channels=Uint8(channels);wanted.samples=2048;gate.configure(rate,channels);paused=true;
            device=SDL_OpenAudioDevice(nullptr,0,&wanted,&actual,0);if(!device)throw std::runtime_error(std::string("SDL PCM: ")+SDL_GetError());
            if(actual.freq!=int(rate)||actual.channels!=channels||actual.format!=AUDIO_S16LSB){close();throw std::runtime_error("SDL changed PCM format");}},
        [this](bool value){gate.pause(value,SDL_GetTicks());update(SDL_GetQueuedAudioSize(device));},
        [this](const uint8_t* data,uint32_t size){update(SDL_GetQueuedAudioSize(device));if(SDL_QueueAudio(device,data,size))throw std::runtime_error(SDL_GetError());update(SDL_GetQueuedAudioSize(device));},
        [this](){const auto bytes=SDL_GetQueuedAudioSize(device);update(bytes);return bytes;},[this](){SDL_ClearQueuedAudio(device);gate.clear(SDL_GetTicks());update(0);}};}
};
}
