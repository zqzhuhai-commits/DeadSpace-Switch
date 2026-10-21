#include "guest_audio.hpp"
#include "guest_sort.hpp"
#include "guest_arena.hpp"
#include "wav_audio.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

static void check(bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);}
template<class F>static void reject(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,"Invalid guest service accepted");}
int main(){try{
    ds::GuestArena arena;const auto p=arena.map(3*4096);check(p&&*p==0x50000000-3*4096,"Anonymous mapping address incorrect");
    check(arena.contains(*p)&&arena.contains(*p+3*4096-1)&&!arena.contains(*p-1),"Anonymous mapping bounds incorrect");
    check(arena.unmap(*p+4096,1)&&!arena.contains(*p+4096)&&arena.mapped_bytes()==8192,"Partial munmap did not split/release pages");
    check(arena.map(4096)==*p+4096,"Freed mapping hole not reused");
    check(!arena.unmap(*p+1,4096)&&!arena.unmap(*p,0)&&!arena.unmap(0xfffff000u,8192),"Invalid munmap accepted");
    check(arena.unmap(*p,12288)&&arena.mapped_bytes()==0,"Whole unmap did not merge available address space");
    for(unsigned i=0;i<1000;++i){const auto big=arena.map(200*1024*1024);check(big.has_value(),"Repeated mmap/munmap exhausted arena");check(arena.unmap(*big,200*1024*1024),"Large munmap failed");}
    check(arena.grow(256*1024*1024)==0x40000000u&&!arena.map(4096),"sbrk/mmap overlap not rejected");
    check(arena.grow(-256*1024*1024)==0x50000000u&&!arena.grow(-1),"sbrk lower bound failed");
    check(!arena.map(0)&&!arena.map(UINT32_MAX),"Anonymous map rounding overflow accepted");
    std::array<uint8_t,20> memory={3,0,0,0,1,0,0,0,2,0,0,0,1,0,0,0,255,255,255,255};
    auto read=[&](uint32_t p){return memory.at(p-0x1000);};auto write=[&](uint32_t p,uint8_t v){memory.at(p-0x1000)=v;};
    auto value=[&](uint32_t p){uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(read(p+i))<<(i*8);return int32_t(v);};
    unsigned comparisons=0;auto cmp=[&](uint32_t a,uint32_t b){check(a>=0x1000&&b>=0x1000&&a<0x1014&&b<0x1014,"Comparator received host pointer");++comparisons;const auto x=value(a),y=value(b);return (x>y)-(x<y);};
    ds::guest_qsort(0x1000,5,4,read,write,cmp);
    const std::array<int32_t,5> sorted={-1,1,1,2,3};for(unsigned i=0;i<5;++i)check(value(0x1000+i*4)==sorted[i],"Guest qsort permutation corrupted");check(comparisons>0,"Comparator not called");
    const auto original=memory;reject([&]{ds::guest_qsort(0x1000,5,4,read,write,[](uint32_t,uint32_t)->int{throw std::runtime_error("guest callback failed");});});check(memory==original,"Failed comparator partially changed array");
    reject([&]{ds::guest_qsort(0xfffffffcu,5,4,read,write,cmp);});reject([&]{ds::guest_qsort(0x1000,65537,4,read,write,cmp);});
    ds::guest_qsort(0,0,UINT32_MAX,read,write,cmp);
    const auto path=std::filesystem::temp_directory_path()/("deadspace-audio-test-"+std::to_string(getpid())+".wav");
    const std::array<uint8_t,8> pcm={0,0x80,0xff,0x7f,0,0,1,0};
    auto backend=ds::wav_audio(path.string());
    {ds::GuestAudioTrack t(backend,44100,2);t.play();check(t.play_state()==3,"Audio play state wrong");check(t.write(pcm.data(),8,true)==8,"WAV did not accept PCM");check(t.peak==32768&&t.nonzero_samples==3,"Signed PCM extrema counted incorrectly");t.pause();check(t.play_state()==2,"Audio pause state wrong");t.stop();check(t.play_state()==1,"Audio stop state wrong");t.release();reject([&]{t.play();});}
    std::ifstream f(path,std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});f.close();std::filesystem::remove(path);
    auto word=[&](unsigned p){uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(bytes.at(p+i))<<(i*8);return v;};
    check(bytes.size()==52&&std::string(bytes.begin(),bytes.begin()+4)=="RIFF"&&word(4)==44&&word(24)==44100&&word(40)==8,"WAV header not finalized");
    check(std::equal(pcm.begin(),pcm.end(),bytes.begin()+44),"Captured PCM bytes changed");
    ds::GuestAudioTrack invalid({},44100,2);reject([&]{invalid.write(pcm.data(),3,true);});reject([&]{invalid.play();});
    std::cout<<"PASS: mmap/munmap reuse, partial unmap, sbrk collisions and overflow; guest qsort callbacks/permutation/failure bounds; real WAV PCM, header finalization, signed extrema and audio states\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
