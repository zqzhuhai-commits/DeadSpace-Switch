#pragma once
#include <algorithm>
#include <cstdint>
namespace ds {
// A host monotonic clock supplies a lower bound for the deterministic scheduler
// clock. Rendering/vsync time must count even when few ARM instructions run.
class GuestClockFloor {
    uint64_t origin=0,elapsed=0;
    bool started=false;
public:
    uint64_t sample(uint64_t host_ns,uint64_t guest_ns) {
        if(!started){origin=host_ns;started=true;}
        if(host_ns>=origin)elapsed=std::max(elapsed,host_ns-origin);
        return std::max(guest_ns,elapsed);
    }
};
}
