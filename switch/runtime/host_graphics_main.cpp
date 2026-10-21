// Actual original-library lifecycle against a host compatibility GL context.
// This executable is diagnostic evidence, not a Switch build or gameplay test.
#include "lifecycle_probe.hpp"
#include "host_egl.hpp"
#include "wav_audio.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
int main(int argc,char** argv){try{
    if(argc<3){std::cerr<<"Usage: ds-game-gl-probe library data_root [frames] [capture.ppm] [capture.wav]\n";return 64;}
    constexpr unsigned width=1280,height=720;
    ds::host::Egl egl(width,height);ds::GraphicsBackend graphics;
    graphics.resolve=[&](const char* name){return egl.resolve(name);};graphics.desktop_compatibility=true;
    const auto read=reinterpret_cast<void(*)(int,int,int,int,unsigned,unsigned,void*)>(egl.resolve("glReadPixels"));
    const auto finish=reinterpret_cast<void(*)()>(egl.resolve("glFinish"));
    const auto error=reinterpret_cast<unsigned(*)()>(egl.resolve("glGetError"));
    if(!read||!finish||!error)throw std::runtime_error("Missing host framebuffer entry points");
    std::vector<uint8_t> pixels(width*height*4);unsigned presented=0,read_error=0;
    graphics.present=[&]{finish();read(0,0,width,height,0x1908,0x1401,pixels.data());read_error=error();++presented;};
    ds::AudioBackend audio;if(argc>5)audio=ds::wav_audio(argv[5]);
    const auto frames=argc>3?unsigned(std::stoul(argv[3])):8;
    if(!frames||frames>10000)throw std::runtime_error("Host frame budget must be 1..10000");
    const auto report=lifecycle_probe(argv[1],std::filesystem::absolute(argv[2]).lexically_normal().string(),frames,std::move(graphics),std::move(audio));
    std::cout<<report;
    uint64_t colored=0;for(size_t i=0;i<pixels.size();i+=4)colored+=pixels[i]!=0||pixels[i+1]!=0||pixels[i+2]!=0;
    std::cout<<"Host readback: frames="<<presented<<" nonblack pixels="<<colored<<" GL error="<<read_error<<" (gameplay unverified)\n";
    if(argc>4&&presented){std::ofstream f(argv[4],std::ios::binary);if(!f)throw std::runtime_error("Cannot create framebuffer capture");f<<"P6\n"<<width<<' '<<height<<"\n255\n";for(int y=height-1;y>=0;--y)for(unsigned x=0;x<width;++x)f.write(reinterpret_cast<const char*>(pixels.data()+(size_t(y)*width+x)*4),3);if(!f)throw std::runtime_error("Framebuffer capture write failed");}
    return report.rfind("PASS:",0)==0&&presented==frames&&!read_error?0:1;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
