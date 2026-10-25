// Host-only hidden WGL reference for the real donor. Not Switch validation.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <GL/gl.h>
#include <mmsystem.h>
#include "lifecycle_probe.hpp"
#include "controller_input.hpp"
#include "hd_texture.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <list>
#include <thread>
#include <chrono>
#include <sstream>
#include <map>
#include <array>
#include <cstring>
#include <cstdlib>

struct Audio {
    HWAVEOUT device=nullptr;
    uint64_t samples=0,nonzero=0;int peak=0;
    struct Block {std::vector<int16_t> pcm;WAVEHDR header{};};
    std::list<Block> queue;
    Audio(){WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=2;format.nSamplesPerSec=44100;format.wBitsPerSample=16;format.nBlockAlign=4;format.nAvgBytesPerSec=176400;
        if(waveOutOpen(&device,WAVE_MAPPER,&format,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR)throw std::runtime_error("PCM output device unavailable");}
    void collect(){for(auto it=queue.begin();it!=queue.end();){if(it->header.dwFlags&WHDR_DONE){waveOutUnprepareHeader(device,&it->header,sizeof(WAVEHDR));it=queue.erase(it);}else ++it;}}
    void playing(bool enabled){if(enabled){if(waveOutRestart(device)!=MMSYSERR_NOERROR)throw std::runtime_error("PCM restart failed");}else{if(waveOutReset(device)!=MMSYSERR_NOERROR)throw std::runtime_error("PCM stop failed");collect();}}
    std::optional<uint32_t> write(const std::vector<int16_t>& pcm){
        collect();if(queue.size()>=8)return std::nullopt;
        if(pcm.empty())return 0;
        queue.emplace_back();auto& block=queue.back();block.pcm=pcm;
        // Keep host investigation quiet; Switch uses the original game gain.
        for(auto& sample:block.pcm)sample=int16_t(sample/8);
        block.header.lpData=reinterpret_cast<char*>(block.pcm.data());block.header.dwBufferLength=DWORD(block.pcm.size()*2);
        if(waveOutPrepareHeader(device,&block.header,sizeof(WAVEHDR))!=MMSYSERR_NOERROR)throw std::runtime_error("PCM prepare failed");
        if(waveOutWrite(device,&block.header,sizeof(WAVEHDR))!=MMSYSERR_NOERROR)throw std::runtime_error("PCM queue failed");
        samples+=pcm.size();for(auto sample:pcm){nonzero+=sample!=0;peak=std::max(peak,std::abs(int(sample)));}
        return uint32_t(pcm.size());
    }
    ~Audio(){if(device){waveOutReset(device);for(auto& block:queue)waveOutUnprepareHeader(device,&block.header,sizeof(WAVEHDR));waveOutClose(device);}}
};

struct Context {
    HWND window=nullptr;HDC dc=nullptr;HGLRC gl=nullptr;HMODULE library=nullptr;
    Context(){
        WNDCLASSW cls{};cls.style=CS_OWNDC;cls.lpfnWndProc=DefWindowProcW;
        cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"DeadSpaceHostReference";
        if(!RegisterClassW(&cls))throw std::runtime_error("RegisterClass failed");
        RECT rectangle{0,0,640,480};AdjustWindowRect(&rectangle,WS_OVERLAPPEDWINDOW,FALSE);
        window=CreateWindowW(cls.lpszClassName,L"Host reference",WS_OVERLAPPEDWINDOW,0,0,rectangle.right-rectangle.left,rectangle.bottom-rectangle.top,nullptr,nullptr,cls.hInstance,nullptr);
        if(!window)throw std::runtime_error("Window creation failed");
        dc=GetDC(window);PIXELFORMATDESCRIPTOR pixel{};
        pixel.nSize=sizeof(pixel);pixel.nVersion=1;pixel.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
        pixel.iPixelType=PFD_TYPE_RGBA;pixel.cColorBits=32;pixel.cDepthBits=24;pixel.cStencilBits=8;
        const int format=ChoosePixelFormat(dc,&pixel);
        if(!format||!SetPixelFormat(dc,format,&pixel))throw std::runtime_error("Pixel format failed");
        gl=wglCreateContext(dc);if(!gl||!wglMakeCurrent(dc,gl))throw std::runtime_error("WGL context failed");
        library=LoadLibraryW(L"opengl32.dll");if(!library)throw std::runtime_error("OpenGL library unavailable");
        glViewport(0,0,640,480);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    }
    ~Context(){if(gl){wglMakeCurrent(nullptr,nullptr);wglDeleteContext(gl);}if(dc)ReleaseDC(window,dc);if(window)DestroyWindow(window);if(library)FreeLibrary(library);}
    void* proc(const char* name){
        auto p=wglGetProcAddress(name);
        if(!p||p==reinterpret_cast<PROC>(1)||p==reinterpret_cast<PROC>(2)||p==reinterpret_cast<PROC>(3)||p==reinterpret_cast<PROC>(-1))p=GetProcAddress(library,name);
        return reinterpret_cast<void*>(p);
    }
};
static void check_fixed_bridge(Context& context){
    std::array<uint32_t,12> args{};std::vector<uint8_t> memory(4096);
    ds::GraphicsBackend backend;backend.desktop_compatibility=true;backend.resolve=[&](const char* name){return context.proc(name);};backend.lighting_strength=.25f;
    ds::GuestGl bridge(backend,{
        [&](unsigned n){return args.at(n);},[&](uint32_t){return 2048u;},
        [&](uint32_t p,void* out,size_t n){if(uint64_t(p)+n>memory.size())throw std::runtime_error("Test read bound");std::memcpy(out,memory.data()+p,n);},
        [&](uint32_t p,const void* in,size_t n){if(uint64_t(p)+n>memory.size())throw std::runtime_error("Test write bound");std::memcpy(memory.data()+p,in,n);}});
    auto call=[&](const char* name,std::initializer_list<uint32_t> values){args.fill(0);std::copy(values.begin(),values.end(),args.begin());bridge.call(name);};
    call("glClearColorx",{32768,0,65536,65536});glClear(GL_COLOR_BUFFER_BIT);
    uint8_t pixel[4]{};glReadPixels(4,4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    if(pixel[0]<127||pixel[0]>128||pixel[1]!=0||pixel[2]!=255)throw std::runtime_error("Fixed color framebuffer mismatch");
    int32_t matrix[16]{};matrix[0]=matrix[5]=matrix[10]=matrix[15]=65536;matrix[12]=98304;matrix[13]=-131072;
    std::memcpy(memory.data()+128,matrix,sizeof(matrix));glMatrixMode(GL_MODELVIEW);call("glLoadMatrixx",{128});
    float actual[16]{};glGetFloatv(GL_MODELVIEW_MATRIX,actual);if(actual[12]!=1.5f||actual[13]!=-2.0f)throw std::runtime_error("Fixed signed matrix mismatch");glLoadIdentity();
    const int32_t light[]={16384,32768,65536,65536};std::memcpy(memory.data()+128,light,sizeof(light));call("glLightxv",{GL_LIGHT0,GL_DIFFUSE,128});
    glGetLightfv(GL_LIGHT0,GL_DIFFUSE,actual);if(actual[0]!=.25f||actual[1]!=.5f||actual[2]!=1)throw std::runtime_error("Fixed light vector mismatch");
    GLuint texture=0;glGenTextures(1,&texture);call("glBindTexture",{GL_TEXTURE_2D,texture});call("glTexParameterx",{GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR});
    GLint filter=0;glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,&filter);if(filter!=GL_LINEAR)throw std::runtime_error("Texture enum was scaled as fixed point");glDeleteTextures(1,&texture);
    if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Fixed bridge GL error");glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    std::cout<<"PASS: real GL fixed color pixel, signed matrix, light vector and unchanged texture enum\n";
    const float vertices[]={-1,-1,0,3,-1,0,-1,3,0};std::memcpy(memory.data()+256,vertices,sizeof(vertices));
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    call("glEnableClientState",{GL_VERTEX_ARRAY});call("glVertexPointer",{3,GL_FLOAT,0,256});
    call("glActiveTexture",{0x84c1});call("glDisable",{GL_TEXTURE_2D});
    call("glDisable",{GL_DEPTH_TEST});call("glEnable",{GL_BLEND});call("glBlendFunc",{GL_DST_COLOR,GL_ONE_MINUS_SRC_ALPHA});
    call("glColor4f",{0x3f800000,0x3f800000,0x3f800000,0});
    const auto clear_sample=[&]{glClearColor(.2f,.1f,.05f,1);glClear(GL_COLOR_BUFFER_BIT);};
    const auto sample=[&](int red){glReadPixels(320,240,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);if(std::abs(int(pixel[0])-red)>2)throw std::runtime_error("Lighting framebuffer pixel mismatch");};
    clear_sample();call("glDrawArrays",{GL_TRIANGLES,0,3});sample(64);
    GLfloat rgba[4]{};GLint active=0;glGetFloatv(GL_CURRENT_COLOR,rgba);glGetIntegerv(0x84e0,&active);
    if(rgba[0]!=1||rgba[1]!=1||rgba[2]!=1||rgba[3]!=0||active!=0x84c1||bridge.lighting_limited_draws!=1)throw std::runtime_error("Lighting compatibility leaked GL state");
    call("glBlendFunc",{GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA});clear_sample();call("glDrawArrays",{GL_TRIANGLES,0,3});sample(51);
    if(bridge.lighting_limited_draws!=1)throw std::runtime_error("Lighting compatibility changed an alpha fade");
    call("glBlendFunc",{GL_DST_COLOR,GL_ONE_MINUS_SRC_ALPHA});glGenTextures(1,&texture);call("glBindTexture",{GL_TEXTURE_2D,texture});
    const uint8_t white[]={255,255,255,255};glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,white);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    call("glEnable",{GL_TEXTURE_2D});clear_sample();call("glDrawArrays",{GL_TRIANGLES,0,3});sample(102);
    if(bridge.lighting_limited_draws!=1)throw std::runtime_error("Lighting compatibility changed a textured draw");
    call("glDisable",{GL_TEXTURE_2D});glDeleteTextures(1,&texture);call("glEnable",{GL_DEPTH_TEST});call("glDepthFunc",{GL_ALWAYS});
    clear_sample();call("glDrawArrays",{GL_TRIANGLES,0,3});sample(102);
    if(bridge.lighting_limited_draws!=1)throw std::runtime_error("Lighting compatibility changed a depth-tested draw");
    call("glActiveTexture",{0x84c0});call("glDisable",{GL_BLEND});call("glDisable",{GL_DEPTH_TEST});call("glDisableClientState",{GL_VERTEX_ARRAY});call("glColor4f",{0x3f800000,0x3f800000,0x3f800000,0x3f800000});call("glDepthFunc",{GL_LESS});call("glBlendFunc",{GL_ONE,GL_ZERO});
    if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Lighting compatibility GL error");glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    std::cout<<"PASS: real GL lighting gain pixel, color/active-unit restoration, alpha-fade/textured/depth-tested bypass\n";

}
int main(int argc,char** argv){
    try {
        if(argc<3){std::cerr<<"Usage: ds-native-host library data_root [frames] [last-frame.bmp]\n";return 64;}
        Context context;check_fixed_bridge(context);Audio audio;
        std::cout<<"Host-only WGL renderer: "<<glGetString(GL_RENDERER)<<"\n";
        ds::GraphicsBackend backend;backend.desktop_compatibility=true;backend.realtime_clock=true;
        if(const auto* strength=std::getenv("DS_LIGHTING_STRENGTH")){char* end=nullptr;const float v=std::strtof(strength,&end);if(end==strength||*end||!(v>=0&&v<=1))throw std::runtime_error("Invalid lighting strength");backend.lighting_strength=v;}
        std::cout<<"Host lighting strength="<<backend.lighting_strength<<"\n";
        const auto host_started=std::chrono::steady_clock::now();
        backend.record=[&](const std::string& text){if(text.rfind("Game frame returned: ",0)==0||text.rfind("INPUT:",0)==0)std::cerr<<"HOST elapsed ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-host_started).count()<<'\n';std::cerr<<text<<'\n';};
        if(argc>6&&std::string(argv[6]).rfind("--hd=",0)==0){const auto hd=std::string(argv[6]).substr(5);backend.hd_budget_bytes=192*1024*1024;backend.hd_texture=[hd](const std::string& key){return ds::read_hd_texture(hd,key);};}
        uint64_t loading_callbacks=0;bool startup_done=false;
        backend.loading=[&](const std::string&,uint64_t,uint64_t){++loading_callbacks;};
        backend.startup_complete=[&]{if(!startup_done){startup_done=true;std::cerr<<"Native player HUD available\n";}};
        backend.resolve=[&](const char* name){return context.proc(name);};
        backend.audio_playing=[&](bool enabled){audio.playing(enabled);};
        backend.audio_write=[&](const std::vector<int16_t>& pcm){return audio.write(pcm);};
        const unsigned frames=argc>3?unsigned(std::stoul(argv[3])):8;
        if(!frames||frames>36000)throw std::runtime_error("Frame count outside host investigation bound");
        unsigned presents=0;
        std::map<unsigned,std::vector<ds::GuestInput>> scheduled;
        std::map<unsigned,ds::ControllerInput::Snapshot> control_changes;
        ds::ControllerInput controller;ds::ControllerInput::Snapshot controls;
        backend.input_mode=[&](bool menu){controller.set_menu(menu);};
        if(argc>5&&std::string(argv[5]).rfind("--events=",0)==0){
            std::ifstream file(std::string(argv[5]).substr(9));if(!file)throw std::runtime_error("Input event script unavailable");
            std::string line;
            while(std::getline(file,line)){
                if(line.empty()||line[0]=='#')continue;
                std::istringstream row(line);unsigned frame;std::string type;ds::GuestInput event;
                if(!(row>>frame>>type))throw std::runtime_error("Invalid event script row");
                if(type=="controls"){ds::ControllerInput::Snapshot snapshot;if(!(row>>snapshot.buttons>>snapshot.lx>>snapshot.ly>>snapshot.rx>>snapshot.ry))throw std::runtime_error("Controller event needs buttons and four axes");control_changes[frame]=snapshot;continue;}
                if(type=="down"||type=="up"||type=="move"){
                    event.kind=ds::GuestInput::Kind::pointer;event.module=1000;event.id=3;event.code=type=="down"?0x6000c:type=="up"?0x8000c:0x4000c;
                    if(!(row>>event.x>>event.y))throw std::runtime_error("Pointer event needs coordinates");
                }else if(type=="key_down"||type=="key_up"){
                    event.kind=ds::GuestInput::Kind::key;event.down=type=="key_down";if(!(row>>event.code))throw std::runtime_error("Key event needs code");
                }else if(type=="quit")event.kind=ds::GuestInput::Kind::quit;
                else throw std::runtime_error("Unknown event script type");
                scheduled[frame].push_back(event);
            }
            backend.poll_input=[&]{const auto change=control_changes.find(presents);if(change!=control_changes.end())controls=change->second;auto events=controller.update(controls,1.0f/60);const auto it=scheduled.find(presents);if(it!=scheduled.end())events.insert(events.end(),it->second.begin(),it->second.end());return events;};
        }
        // Explicit opt-in synthetic menu tap for host input investigation.
        if(argc>5&&std::string(argv[5])=="--tap")backend.poll_input=[&]{
            std::vector<ds::GuestInput> events;
            if(presents==700||presents==702||presents==1400||presents==1402){ds::GuestInput event;event.kind=ds::GuestInput::Kind::pointer;event.code=(presents==700||presents==1400)?0x6000c:0x8000c;event.module=1000;event.id=3;event.x=320;event.y=240;events.push_back(event);}
            if(presents==900||presents==902){ds::GuestInput event;event.kind=ds::GuestInput::Kind::key;event.code=23;event.down=presents==900;events.push_back(event);}
            return events;
        };
        backend.present=[&]{
            ++presents;
            if(argc>4&&(presents==frames||presents%300==0)){
                std::vector<uint8_t> pixels(640*480*4);glReadBuffer(GL_BACK);glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
                if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Framebuffer readback failed");
                for(size_t i=0;i<pixels.size();i+=4)std::swap(pixels[i],pixels[i+2]);
                BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+DWORD(pixels.size());
                BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=640;info.biHeight=480;info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;
                const auto filename=std::string(argv[4])+(presents==frames?"":"."+std::to_string(presents)+".bmp");
                std::ofstream output(filename,std::ios::binary);output.write(reinterpret_cast<const char*>(&file),sizeof(file));output.write(reinterpret_cast<const char*>(&info),sizeof(info));output.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());
                if(!output)throw std::runtime_error("Framebuffer evidence file failed");
            }
            SwapBuffers(context.dc);MSG event;while(PeekMessageW(&event,nullptr,0,0,PM_REMOVE)){TranslateMessage(&event);DispatchMessageW(&event);}
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        };
        const auto report=lifecycle_probe(argv[1],argv[2],frames,std::move(backend));
        std::cout<<report<<"Presented frames: "<<presents<<"\n";
        std::cout<<"Loading callbacks: "<<loading_callbacks<<"; startup complete: "<<startup_done<<"\n";
        std::cout<<"PCM output: samples="<<audio.samples<<" nonzero="<<audio.nonzero<<" peak="<<audio.peak<<" (device queue accepted; listening not verified)\n";
        std::cout<<"Host reference only; gameplay, audible audio and Switch hardware remain unverified.\n";
        return report.rfind("PASS:",0)==0?0:2;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
