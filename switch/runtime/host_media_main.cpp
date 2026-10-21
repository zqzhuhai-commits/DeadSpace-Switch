#include "lifecycle_probe.hpp"
#include "hd_texture.hpp"
#include "sdl_audio_output.hpp"
#include "host_egl.hpp"
#include <charconv>
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
int main(int argc,char** argv){
    if(argc<4||argc>7){std::cerr<<"Usage: ds-media-probe library data_root frames [hd_root] [frame.ppm] [touch.tsv]\n";return 64;}
    unsigned frames=0;const std::string count=argv[3];const auto number=std::from_chars(count.data(),count.data()+count.size(),frames);
    if(number.ec!=std::errc{}||number.ptr!=count.data()+count.size()||!frames||frames>100000)return 64;
    try{
        HostEgl egl;ds::SdlAudioOutput audio;ds::GraphicsBackend graphics;
        graphics.width=1280;graphics.height=720;graphics.resolve=[&](const char* name){return egl.resolve(name);};graphics.desktop_compatibility=true;
        unsigned presented=0;graphics.present=[&]{++presented;};
        if(argc>4){const std::string root=argv[4];graphics.hd_texture=[root](const std::string& key){return ds::read_hd_texture(root,key);};}
        std::map<unsigned,std::vector<ds::InputEvent>> inputs;
        if(argc>6){
            std::ifstream script(argv[6]);if(!script)throw std::runtime_error("Cannot read touch script");std::string line;unsigned lines=0;
            while(std::getline(script,line)){
                if(++lines>10000||line.size()>256)throw std::runtime_error("Touch script bound");
                if(line.empty())continue;
                unsigned at;std::string kind,extra;float x,y;std::istringstream row(line);
                if(!(row>>at>>kind>>x>>y)||at>=frames||(row>>extra))throw std::runtime_error("Invalid touch script row");
                if(kind!="down"&&kind!="up"&&kind!="move")throw std::runtime_error("Invalid touch script action");
                const auto k=kind=="down"?ds::InputEvent::touch_down:kind=="up"?ds::InputEvent::touch_up:ds::InputEvent::touch_move;
                ds::InputEvent event{k,0,0,x,y};ds::pointer_arguments(event,1280,720);inputs[at].push_back(event);
            }
            graphics.input=[&](unsigned frame){const auto it=inputs.find(frame);return it==inputs.end()?std::vector<ds::InputEvent>{}:it->second;};
        }
        const auto result=lifecycle_probe(argv[1],argv[2],frames,std::move(graphics),audio.backend());
        std::cout<<result<<"Frames presented: "<<presented<<"\nAudio driver: "<<(SDL_GetCurrentAudioDriver()?SDL_GetCurrentAudioDriver():"not initialized")<<" (host output only; listening unverified)\n";
        if(argc>5){
            auto read=reinterpret_cast<void(*)(int,int,int,int,unsigned,unsigned,void*)>(egl.resolve("glReadPixels"));auto finish=reinterpret_cast<void(*)()>(egl.resolve("glFinish"));require(read&&finish,"Framebuffer capture unavailable");finish();
            std::vector<uint8_t> rgb(1280*720*3);read(0,0,1280,720,0x1907,0x1401,rgb.data());std::ofstream out(argv[5],std::ios::binary);out<<"P6\n1280 720\n255\n";
            for(int y=719;y>=0;--y)out.write(reinterpret_cast<const char*>(rgb.data()+y*1280*3),1280*3);
            if(!out)throw std::runtime_error("Framebuffer capture write failed");
        }
        audio.close();SDL_QuitSubSystem(SDL_INIT_AUDIO);return result.rfind("PASS:",0)==0?0:1;
    }catch(const std::exception& e){std::cerr<<"Host media probe failed: "<<e.what()<<'\n';return 1;}
}
