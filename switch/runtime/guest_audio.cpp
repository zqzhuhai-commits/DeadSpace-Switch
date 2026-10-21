#include "guest_jni.hpp"
#include <limits>
#include <algorithm>

namespace ds {
void GuestJni::configure_audio(uint32_t ref,uint32_t rate,uint32_t channels){
    if(audio_tracks.count(ref))throw std::runtime_error("AudioTrack configured twice");
    audio_tracks.emplace(ref,std::make_unique<GuestAudioTrack>(audio_backend,rate,channels));
}
std::optional<uint64_t> GuestJni::audio_delegate(uint32_t ref,const MethodKey& m,const std::vector<uint64_t>& v){
    if(object(ref).cls!="android/media/AudioTrack")return std::nullopt;
    if(m.name=="getMinBufferSize"&&m.sig=="(III)I"){
        if(v.at(0)<8000||v.at(0)>192000||v.at(2)!=2||(v.at(1)!=4&&v.at(1)!=12&&v.at(1)!=2))return uint32_t(-2);
        return 4096;
    }
    if(m.name=="<init>"&&m.sig=="(IIIIII)V"){
        const auto channel=uint32_t(v.at(2));
        if(v.at(3)!=2||v.at(5)!=1||(channel!=4&&channel!=12&&channel!=2))throw std::runtime_error("AudioTrack requires streaming PCM16 mono/stereo");
        configure_audio(ref,uint32_t(v.at(1)),channel==4?1:2);return 0;
    }
    const auto it=audio_tracks.find(ref);if(it==audio_tracks.end())throw std::runtime_error("Unconfigured AudioTrack receiver");
    auto& track=*it->second;
    if(m.name=="play"&&m.sig=="()V"){track.play();return 0;}
    if(m.name=="pause"&&m.sig=="()V"){track.pause();return 0;}
    if(m.name=="stop"&&m.sig=="()V"){track.stop();return 0;}
    if(m.name=="flush"&&m.sig=="()V"){track.flush();return 0;}
    if(m.name=="release"&&m.sig=="()V"){track.release();return 0;}
    if(m.name=="getState"&&m.sig=="()I")return track.valid()?1:0;
    if(m.name=="getPlayState"&&m.sig=="()I")return track.play_state();
    if(m.name!="write"||(m.sig!="([SII)I"&&m.sig!="([BII)I"&&m.sig!="([BIII)I"))return std::nullopt;
    const bool shorts=m.sig[2]=='S';
    const auto& array=object(uint32_t(v.at(0)));
    const auto off=uint32_t(v.at(1)),count=uint32_t(v.at(2)),step=shorts?2u:1u;
    if(array.cls!=(shorts?"[S":"[B")||off>array.length||count>array.length-off||count>1024*1024/step)return uint32_t(-2);
    const auto mode=v.size()>3?uint32_t(v[3]):0;
    if(mode>1||(uint64_t(count)*step)%(track.channels*2))return uint32_t(-2);
    if(!track.valid())return uint32_t(-3);
    if(!count)return 0;
    const uint64_t start=uint64_t(array.elements.at(0))+uint64_t(off)*step;
    if(start+uint64_t(count)*step>uint64_t(UINT32_MAX)+1)throw std::runtime_error("AudioTrack array address wraps");
    std::vector<uint8_t> pcm(count*step);
    for(uint32_t i=0;i<pcm.size();++i)pcm[i]=MemoryRead8(uint32_t(start+i));
    return track.write(pcm.data(),uint32_t(pcm.size()),mode==0)/step;
}
std::string GuestJni::audio_report()const{
    uint64_t bytes=0,writes=0,nonzero=0;uint32_t peak=0;
    for(const auto& kv:audio_tracks){const auto& t=*kv.second;bytes+=t.bytes;writes+=t.write_calls;nonzero+=t.nonzero_samples;peak=std::max(peak,t.peak);}
    return "Audio backend: "+(audio_backend.description.empty()?"unavailable":audio_backend.description)+"; PCM bytes="+std::to_string(bytes)+" writes="+std::to_string(writes)+" nonzero samples="+std::to_string(nonzero)+" peak="+std::to_string(peak)+" (speaker output unverified).\n";
}
}
