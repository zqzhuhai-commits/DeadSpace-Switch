#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace ds {
// Signed little-endian 16-bit interleaved PCM. The host owns the device.
struct AudioBackend {
    std::function<void(unsigned,unsigned)> open;
    std::function<void(bool)> pause;
    std::function<void(const uint8_t*,uint32_t)> queue;
    std::function<uint32_t()> queued;
    std::function<void()> clear;
};
struct AudioBackpressure {};
class GuestAudio {
    AudioBackend backend;
    uint32_t track=0;
    unsigned rate=0,channels=0,state=1;
    bool opened=false,released=false;
    void check(uint32_t ref)const{if(!track||ref!=track||released)throw std::runtime_error("Invalid/released AudioTrack");}
public:
    uint64_t submitted_bytes=0,write_calls=0,nonzero_samples=0;
    uint32_t peak=0;
    void set_backend(AudioBackend b){if(opened)throw std::runtime_error("Audio backend already open");backend=std::move(b);}
    void attach(uint32_t ref,unsigned hz,unsigned count){
        if(track||!ref||hz<8000||hz>192000||(count!=1&&count!=2))throw std::runtime_error("Unsupported PCM configuration");
        track=ref;rate=hz;channels=count;
    }
    void play(uint32_t ref){
        check(ref);
        if(!opened){if(!backend.open||!backend.pause||!backend.queue||!backend.queued||!backend.clear)throw std::runtime_error("AudioTrack requires a PCM output backend");backend.open(rate,channels);opened=true;}
        backend.pause(false);state=3;
    }
    void pause(uint32_t ref){check(ref);if(opened)backend.pause(true);state=2;}
    void stop(uint32_t ref){check(ref);if(opened){backend.pause(true);backend.clear();}state=1;}
    void flush(uint32_t ref){check(ref);if(opened)backend.clear();}
    void release(uint32_t ref){stop(ref);released=true;}
    uint32_t play_state(uint32_t ref)const{check(ref);return state;}
    uint32_t initialized(uint32_t ref)const{return ref==track&&!released?1:0;}
    uint32_t write(uint32_t ref,const std::vector<uint8_t>& pcm,unsigned unit,bool blocking){
        check(ref);
        if((unit!=1&&unit!=2)||pcm.size()>1024*1024||pcm.size()%(channels*2))throw std::runtime_error("PCM block size/alignment invalid");
        if(pcm.empty())return 0;
        if(!opened){play(ref);pause(ref);}
        const uint32_t limit=std::max<uint32_t>(8192,rate*channels*2/10),used=backend.queued();
        uint32_t count=uint32_t(std::min<uint64_t>(pcm.size(),used>=limit?0:limit-used));count-=count%(channels*2);
        if(!count){if(blocking)throw AudioBackpressure{};return 0;}
        backend.queue(pcm.data(),count);++write_calls;submitted_bytes+=count;
        for(uint32_t i=0;i<count;i+=2){const int32_t s=int16_t(uint16_t(pcm[i])|uint16_t(pcm[i+1])<<8);if(s)++nonzero_samples;peak=std::max(peak,uint32_t(s<0?-s:s));}
        return count/unit;
    }
};
}
