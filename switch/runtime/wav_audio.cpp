#include "wav_audio.hpp"
#include <array>
#include <cstdio>

namespace ds {
namespace {
class WavSink final:public AudioSink {
    std::FILE* file=nullptr;
    uint32_t rate,channels,size=0;
    bool active=false;
    void header(){
        std::array<uint8_t,44> h{};
        auto put=[&](unsigned p,uint32_t v,unsigned n){for(unsigned i=0;i<n;++i)h[p+i]=uint8_t(v>>(i*8));};
        const char* chunks[]={"RIFF","WAVE","fmt ","data"};const unsigned offsets[]={0,8,12,36};
        for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)h[offsets[i]+j]=uint8_t(chunks[i][j]);
        put(4,36+size,4);put(16,16,4);put(20,1,2);put(22,channels,2);put(24,rate,4);put(28,rate*channels*2,4);put(32,channels*2,2);put(34,16,2);put(40,size,4);
        std::fseek(file,0,SEEK_SET);std::fwrite(h.data(),1,h.size(),file);std::fflush(file);
    }
public:
    WavSink(const std::string& path,uint32_t hz,uint32_t ch):rate(hz),channels(ch){file=std::fopen(path.c_str(),"wbx");if(!file)throw std::runtime_error("Cannot create exclusive WAV capture");header();std::fseek(file,44,SEEK_SET);}
    ~WavSink()override{if(file){header();std::fclose(file);}}
    void playing(bool v)override{active=v;}
    void clear()override{} // Capture is an append-only record, not a speaker queue.
    uint32_t write(const uint8_t* pcm,uint32_t n,bool)override{
        if(!active)throw std::runtime_error("WAV capture is paused");
        if(n>16*1024*1024-size)throw std::runtime_error("WAV diagnostic capture reached 16 MiB bound");
        if(std::fwrite(pcm,1,n,file)!=n)throw std::runtime_error("WAV PCM write failed");
        size+=n;return n;
    }
};
}
AudioBackend wav_audio(const std::string& path){
    auto opened=std::make_shared<bool>(false);
    return {"WAV capture (no speaker)",[path,opened](uint32_t rate,uint32_t channels)->std::unique_ptr<AudioSink>{if(*opened)throw std::runtime_error("WAV capture supports one track");auto sink=std::make_unique<WavSink>(path,rate,channels);*opened=true;return sink;}};
}
}
