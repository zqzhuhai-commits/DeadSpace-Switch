#pragma once
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
namespace ds {
class FrameTiming {
    std::function<void(const std::string&)> record;
    double frequency;
    uint64_t previous=0,total=0,present=0,maximum=0;
    unsigned frames=0;
public:
    FrameTiming(double hz,std::function<void(const std::string&)> sink):record(std::move(sink)),frequency(hz){}
    void complete(uint64_t started,uint64_t finished){
        if(previous){
            const uint64_t elapsed=finished-previous;
            total+=elapsed;present+=finished-started;
            if(elapsed>maximum)maximum=elapsed;
            if(++frames==120){
                char text[192];
                if(record&&frequency>0){
                    std::snprintf(text,sizeof(text),"Frame timing (120): mean_ms=%.2f max_ms=%.2f present_mean_ms=%.2f",total*1000.0/frequency/120,maximum*1000.0/frequency,present*1000.0/frequency/120);
                    record(text);
                }
                total=present=maximum=0;frames=0;
            }
        }
        previous=finished;
    }
};
}
