#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace ds {
// PCM is always signed 16-bit little endian; lengths are bytes.
class AudioSink {
public:
    virtual ~AudioSink()=default;
    virtual void playing(bool)=0;
    virtual void clear()=0;
    virtual uint32_t write(const uint8_t*,uint32_t,bool blocking)=0;
};
struct AudioBackend {
    std::string description;
    std::function<std::unique_ptr<AudioSink>(uint32_t rate,uint32_t channels)> open;
};
class GuestAudioTrack {
    AudioBackend backend;
    std::unique_ptr<AudioSink> sink;
    bool released=false,active=false;
    uint32_t playback=1;
public:
    uint32_t rate,channels;
    uint64_t bytes=0,nonzero_samples=0,write_calls=0;
    uint32_t peak=0;
    GuestAudioTrack(AudioBackend b,uint32_t hz,uint32_t ch):backend(std::move(b)),rate(hz),channels(ch){
        if(hz<8000||hz>192000||(ch!=1&&ch!=2))throw std::runtime_error("Unsupported AudioTrack PCM configuration");
    }
    void play(){
        if(released)throw std::runtime_error("AudioTrack already released");
        if(!sink){if(!backend.open)throw std::runtime_error("AudioTrack output backend unavailable");sink=backend.open(rate,channels);if(!sink)throw std::runtime_error("AudioTrack output open failed");}
        sink->playing(true);active=true;playback=3;
    }
    void pause(){if(released)throw std::runtime_error("AudioTrack already released");if(sink)sink->playing(false);active=false;playback=2;}
    void stop(){pause();if(sink)sink->clear();playback=1;}
    void flush(){if(released)throw std::runtime_error("AudioTrack already released");if(sink)sink->clear();}
    void release(){sink.reset();active=false;released=true;playback=1;}
    bool playing()const{return active;}
    uint32_t play_state()const{return playback;}
    bool valid()const{return !released;}
    uint32_t write(const uint8_t* pcm,uint32_t length,bool blocking){
        if(released)throw std::runtime_error("AudioTrack already released");
        if(length>1024*1024||length%(channels*2))throw std::runtime_error("Unaligned/oversized AudioTrack PCM write");
        if(!length)return 0;
        if(!active)play();
        const auto accepted=sink->write(pcm,length,blocking);
        if(accepted>length||accepted%(channels*2))throw std::runtime_error("Audio backend returned invalid PCM length");
        for(uint32_t i=0;i<accepted;i+=2){const int32_t v=int16_t(uint16_t(pcm[i])|uint16_t(pcm[i+1])<<8);const auto magnitude=uint32_t(v<0?-v:v);nonzero_samples+=magnitude!=0;if(magnitude>peak)peak=magnitude;}
        bytes+=accepted;++write_calls;return accepted;
    }
};
}
