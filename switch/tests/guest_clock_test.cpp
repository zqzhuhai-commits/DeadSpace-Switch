#include "guest_clock.hpp"
#include "guest_threads.hpp"
#include <stdexcept>
#include <cstdint>
void require(bool value){if(!value)throw std::runtime_error("Guest clock regression");}
int main(){
    ds::GuestClockFloor clock;
    ds::GuestThreads threads;
    const uint64_t origin=1000000000;
    require(clock.sample(origin,threads.time())==0);
    // One second at 60 Hz with tiny guest work must still advance one second.
    for(uint64_t frame=1;frame<=60;++frame){
        threads.tick(1000);
        const auto next=clock.sample(origin+frame*1000000000/60,threads.time());
        threads.tick(next-threads.time());
    }
    require(threads.time()==1000000000);
    // Preserve scheduler sleep fast-forward and tolerate a regressing source.
    require(clock.sample(origin+900000000,1500000000)==1500000000);
    require(clock.sample(origin-1,0)==1000000000);
    // Sleep deadlines become ready from elapsed host time, not ARM throughput.
    threads.sleep(100000000);
    const auto target=clock.sample(origin+1200000000,threads.time());
    threads.tick(target-threads.time());threads.select();
    require(threads.thread().state==ds::GuestThreads::State::ready);
}
