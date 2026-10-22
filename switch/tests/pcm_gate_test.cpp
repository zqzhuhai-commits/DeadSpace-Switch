#include "pcm_buffer_gate.hpp"
#include <stdexcept>
#include <iostream>
static void check(bool v,const char* s){if(!v)throw std::runtime_error(s);}
int main(){
    ds::PcmBufferGate gate;gate.configure(44100,2);gate.pause(false,0);
    check(gate.paused(0,0),"Empty output starts too soon");
    check(gate.paused(4096,20),"Small first block must prebuffer");
    check(!gate.paused(17640,40)&&gate.starts==1,"100ms prebuffer did not start");
    check(!gate.paused(4,60),"Low queue must keep playing until actual underrun");
    check(gate.paused(0,70)&&gate.underruns==1,"Underrun must reprime once");
    check(gate.paused(0,80)&&gate.underruns==1,"Empty priming queue double-counts underrun");
    check(gate.paused(400,100),"Small refill starts too soon");
    check(!gate.paused(400,220)&&gate.starts==2,"Short final block must play after deadline");
    gate.pause(true,230);check(gate.paused(40000,240),"Explicit pause ignored");
    gate.pause(false,250);check(!gate.paused(40000,250),"Buffered resume stalled");
    gate.clear(260);check(gate.paused(0,260),"Flush must reprime");
    gate.pause(true,0xfffffff0);gate.pause(false,0xfffffff0);
    check(!gate.paused(4,0x90),"Timer wrap lost short-block deadline");
    std::cout<<"PASS: PCM prefill, underrun recovery, bounded short-block delay, pause/flush and timer wrap\n";
}
