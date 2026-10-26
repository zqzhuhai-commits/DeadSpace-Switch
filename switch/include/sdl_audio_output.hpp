#pragma once
#include "guest_audio.hpp"
#ifdef __SWITCH__
#include <SDL2/SDL.h>
#else
#include <SDL.h>
#endif
namespace ds {
class SdlAudioOutput {
    SDL_AudioDeviceID device=0;
public:
    SdlAudioOutput()=default;
    SdlAudioOutput(const SdlAudioOutput&)=delete;
    SdlAudioOutput& operator=(const SdlAudioOutput&)=delete;
    ~SdlAudioOutput(){close();}
    void close(){if(device){SDL_ClearQueuedAudio(device);SDL_CloseAudioDevice(device);device=0;}}
    AudioBackend backend(){return {
        [this](unsigned rate,unsigned channels){
            if(device)throw std::runtime_error("SDL PCM output already open");
            if(SDL_InitSubSystem(SDL_INIT_AUDIO))throw std::runtime_error(SDL_GetError());
            SDL_AudioSpec wanted{},actual{};wanted.freq=int(rate);wanted.format=AUDIO_S16LSB;wanted.channels=Uint8(channels);wanted.samples=1024;
            device=SDL_OpenAudioDevice(nullptr,0,&wanted,&actual,0);if(!device)throw std::runtime_error(std::string("SDL PCM: ")+SDL_GetError());
            if(actual.freq!=int(rate)||actual.channels!=channels||actual.format!=AUDIO_S16LSB){close();throw std::runtime_error("SDL changed PCM format");}},
        [this](bool paused){SDL_PauseAudioDevice(device,paused?1:0);},
        [this](const uint8_t* data,uint32_t size){if(SDL_QueueAudio(device,data,size))throw std::runtime_error(SDL_GetError());},
        [this](){return SDL_GetQueuedAudioSize(device);},[this](){SDL_ClearQueuedAudio(device);}};}
};
}
