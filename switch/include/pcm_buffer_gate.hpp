#pragma once
#include <cstdint>
namespace ds {
// Queue startup/recovery hysteresis. Time is supplied for deterministic tests.
class PcmBufferGate {
    uint32_t target=0,waiting_since=0;
    bool playing=false,priming=true;
public:
    uint64_t underruns=0,starts=0;
    void configure(unsigned rate,unsigned channels){target=rate*channels*2/10;playing=false;priming=true;underruns=starts=0;}
    void pause(bool value,uint32_t now){
        if(value){playing=false;priming=true;}
        else if(!playing){playing=true;priming=true;waiting_since=now;}
    }
    void clear(uint32_t now){priming=true;waiting_since=now;}
    bool paused(uint32_t queued,uint32_t now){
        if(!playing)return true;
        if(!priming&&!queued){++underruns;priming=true;waiting_since=now;}
        // Do not leave a short final block silent indefinitely.
        if(priming&&queued&&(queued>=target||uint32_t(now-waiting_since)>=150)){priming=false;++starts;}
        return priming;
    }
};
}
