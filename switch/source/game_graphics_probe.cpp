#include "platform_probe.hpp"
#ifdef DS_CPU_PROBE
#include "lifecycle_probe.hpp"
#include "hd_texture.hpp"
#include "app_paths.hpp"
#include "startup_log.hpp"
#include <SDL2/SDL.h>
#include <cstdio>
#include <stdexcept>
#include <switch.h>
#include "controller_input.hpp"
#include "gyro_input.hpp"
#include "loading_overlay.hpp"
#include <memory>

std::string game_graphics_probe(const std::string& root,unsigned frames) {
    std::string log="Original-library lifecycle with native GLES1 subset (not verified gameplay)\n";
    ds::startup_log("BEGIN: SDL video/audio init");
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO)<0){log+=SDL_GetError();ds::startup_log("FAIL: SDL init: %s",SDL_GetError());SDL_Quit();return log;}
    ds::startup_log("END: SDL init; BEGIN: GLES1 window/context");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
#ifdef DS_GAME_LAUNCHER
    int requested_width=1280,requested_height=720;
    if(auto* config=std::fopen("sdmc:/switch/deadspacemobile/display.cfg","r")){
        int w=0,h=0;if(std::fscanf(config,"%d %d",&w,&h)==2&&((w==1280&&h==720)||(w==960&&h==540))){requested_width=w;requested_height=h;}std::fclose(config);
    }else if(auto* created=std::fopen("sdmc:/switch/deadspacemobile/display.cfg","w")){std::fputs("1280 720\n",created);std::fclose(created);}
    ds::startup_log("Requested surface: %dx%d",requested_width,requested_height);
    SDL_Window* window=SDL_CreateWindow("Dead Space Mobile",0,0,requested_width,requested_height,SDL_WINDOW_OPENGL|SDL_WINDOW_SHOWN);
#else
    SDL_Window* window=SDL_CreateWindow("Dead Space Mobile native GL diagnostics",0,0,1280,720,SDL_WINDOW_OPENGL|SDL_WINDOW_SHOWN);
#endif
    SDL_GLContext context=window?SDL_GL_CreateContext(window):nullptr;
    if(!context){log+="Native GLES1 context unavailable: "+std::string(SDL_GetError())+"\n";ds::startup_log("FAIL: GLES1 context: %s",SDL_GetError());if(window)SDL_DestroyWindow(window);SDL_Quit();return log;}
    ds::startup_log("END: GLES1 context");
    auto get_string=reinterpret_cast<const unsigned char*(*)(unsigned)>(SDL_GL_GetProcAddress("glGetString"));
    if(get_string){const auto* renderer=get_string(0x1f01);log+="Renderer: "+std::string(renderer?reinterpret_cast<const char*>(renderer):"unknown")+"\n";}
    ds::GraphicsBackend graphics;
    graphics.realtime_clock=true;
    SDL_GL_SetSwapInterval(1);
    int drawable_width=1280,drawable_height=720;SDL_GL_GetDrawableSize(window,&drawable_width,&drawable_height);
    graphics.surface_width=uint32_t(drawable_width);graphics.surface_height=uint32_t(drawable_height);
    std::unique_ptr<std::FILE,int(*)(std::FILE*)> progress(std::fopen(ds::paths::log_file,"a"),std::fclose);
    graphics.record=[&](const std::string& text){ds::startup_log("%s",text.c_str());if(progress){std::fprintf(progress.get(),"%s\n",text.c_str());std::fflush(progress.get());}};
    graphics.record("Native surface: "+std::to_string(drawable_width)+"x"+std::to_string(drawable_height));
    bool starting=true;
    uint64_t loading_bytes=0,loading_frames=0;
    const uint32_t loading_started=SDL_GetTicks();uint32_t last_loading_draw=0;
    auto draw_loading=[&]{
        if(!starting)return;
        auto gi=reinterpret_cast<void(*)(unsigned,int*)>(SDL_GL_GetProcAddress("glGetIntegerv"));
        auto gf=reinterpret_cast<void(*)(unsigned,float*)>(SDL_GL_GetProcAddress("glGetFloatv"));
        auto gb=reinterpret_cast<void(*)(unsigned,unsigned char*)>(SDL_GL_GetProcAddress("glGetBooleanv"));
        auto enabled=reinterpret_cast<unsigned char(*)(unsigned)>(SDL_GL_GetProcAddress("glIsEnabled"));
        auto en=reinterpret_cast<void(*)(unsigned)>(SDL_GL_GetProcAddress("glEnable"));auto dis=reinterpret_cast<void(*)(unsigned)>(SDL_GL_GetProcAddress("glDisable"));
        auto sc=reinterpret_cast<void(*)(int,int,int,int)>(SDL_GL_GetProcAddress("glScissor"));
        auto cc=reinterpret_cast<void(*)(float,float,float,float)>(SDL_GL_GetProcAddress("glClearColor"));
        auto cm=reinterpret_cast<void(*)(unsigned char,unsigned char,unsigned char,unsigned char)>(SDL_GL_GetProcAddress("glColorMask"));auto cl=reinterpret_cast<void(*)(unsigned)>(SDL_GL_GetProcAddress("glClear"));
        if(!gi||!gf||!gb||!enabled||!en||!dis||!sc||!cc||!cm||!cl)throw std::runtime_error("Loading overlay GL entry missing");
        int box[4];float saved[4];unsigned char mask[4];gi(0x0c10,box);gf(0x0c22,saved);gb(0x0c23,mask);const auto was_enabled=enabled(0x0c11);
        en(0x0c11);cm(1,1,1,1);
        ds::draw_loading(uint32_t(SDL_GetTicks()-loading_started),loading_bytes,loading_frames,[&](int x,int y,int w,int h,float r,float g,float b){
            const int x0=x*drawable_width/640,x1=(x+w)*drawable_width/640;
            const int y0=(480-y-h)*drawable_height/480,y1=(480-y)*drawable_height/480;
            sc(x0,y0,x1-x0,y1-y0);cc(r,g,b,1);cl(0x4000);
        });
        cc(saved[0],saved[1],saved[2],saved[3]);cm(mask[0],mask[1],mask[2],mask[3]);sc(box[0],box[1],box[2],box[3]);if(!was_enabled)dis(0x0c11);
        last_loading_draw=SDL_GetTicks();
    };
    graphics.startup_complete=[&,record=graphics.record]{if(starting){starting=false;record("Loading overlay dismissed: native player HUD available");}};
    graphics.loading=[&](const std::string&,uint64_t bytes,uint64_t frames){
        loading_bytes=bytes;loading_frames=frames;
        if(starting&&uint32_t(SDL_GetTicks()-last_loading_draw)>=250){draw_loading();SDL_GL_SwapWindow(window);}
    };
    auto initial_color=reinterpret_cast<void(*)(float,float,float,float)>(SDL_GL_GetProcAddress("glClearColor"));
    auto initial_clear=reinterpret_cast<void(*)(unsigned)>(SDL_GL_GetProcAddress("glClear"));
    if(!initial_color||!initial_clear)throw std::runtime_error("Initial loading GL entry missing");
    initial_color(0.015f,0.025f,0.04f,1);initial_clear(0x4000);
    draw_loading();SDL_GL_SwapWindow(window);
    SDL_AudioSpec desired{};desired.freq=44100;desired.format=AUDIO_S16LSB;desired.channels=2;desired.samples=1024;
    ds::startup_log("BEGIN: SDL audio device");
    SDL_AudioSpec actual{};const auto audio=SDL_OpenAudioDevice(nullptr,0,&desired,&actual,0);
    if(!audio){log+="Audio device unavailable: "+std::string(SDL_GetError())+"\n";ds::startup_log("FAIL: audio device: %s",SDL_GetError());SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();return log;}
    ds::startup_log("END: SDL audio device");
    graphics.audio_playing=[audio](bool enabled){if(!enabled)SDL_ClearQueuedAudio(audio);SDL_PauseAudioDevice(audio,enabled?0:1);};
    graphics.audio_write=[audio](const std::vector<int16_t>& pcm)->std::optional<uint32_t>{
        if(SDL_GetQueuedAudioSize(audio)>44100*4/4)return std::nullopt;
        if(SDL_QueueAudio(audio,pcm.data(),uint32_t(pcm.size()*2))<0)throw std::runtime_error(SDL_GetError());
        return uint32_t(pcm.size());
    };
    graphics.resolve=[](const char* name){return SDL_GL_GetProcAddress(name);};
    PadState pad;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);hidInitializeTouchScreen();
    ds::ControllerInput input;
    graphics.input_mode=[&](bool menu){input.set_menu(menu);};
    struct Sensors {
        HidSixAxisSensorHandle handles[4]{};bool active[4]{};ds::GyroInput filter;
        Sensors(){
            const Result a=hidGetSixAxisSensorHandles(handles,1,HidNpadIdType_Handheld,HidNpadStyleTag_NpadHandheld);
            const Result b=hidGetSixAxisSensorHandles(handles+1,1,HidNpadIdType_No1,HidNpadStyleTag_NpadFullKey);
            const Result c=hidGetSixAxisSensorHandles(handles+2,2,HidNpadIdType_No1,HidNpadStyleTag_NpadJoyDual);
            for(int i=0;i<4;++i)if(R_SUCCEEDED(i==0?a:i==1?b:c))active[i]=R_SUCCEEDED(hidStartSixAxisSensor(handles[i]));
            ds::startup_log("Motion sensors: handheld=%u Pro=%u JoyConR=%u",unsigned(active[0]),unsigned(active[1]),unsigned(active[3]));
        }
        ~Sensors(){for(int i=0;i<4;++i)if(active[i])hidStopSixAxisSensor(handles[i]);}
        std::pair<float,float> read(bool handheld,uint32_t style,float dt){
            const int i=handheld?0:(style&HidNpadStyleTag_NpadJoyDual)?3:1;HidSixAxisSensorState state{};
            if(!active[i]||hidGetSixAxisSensorStates(handles[i],&state,1)<1)return {0,0};
            return filter.update(state.acceleration.x,state.acceleration.y,state.acceleration.z,state.angular_velocity.x,state.angular_velocity.y,state.angular_velocity.z,dt);
        }
    } sensors;
    bool motion_was_enabled=false;
    uint32_t last_tick=SDL_GetTicks();bool touched=false;float touch_x=0,touch_y=0;
    graphics.poll_input=[&]{
        using C=ds::ControllerInput;
        std::vector<ds::GuestInput> events;
        SDL_Event event;bool quit=!appletMainLoop();while(SDL_PollEvent(&event))if(event.type==SDL_QUIT)quit=true;
        padUpdate(&pad);const auto buttons=padGetButtons(&pad);uint32_t held=0;
        const std::pair<uint64_t,uint32_t> map[]={{HidNpadButton_A,C::A},{HidNpadButton_B,C::B},{HidNpadButton_X,C::X},{HidNpadButton_Y,C::Y},{HidNpadButton_L,C::L},{HidNpadButton_R,C::R},{HidNpadButton_ZL,C::ZL},{HidNpadButton_ZR,C::ZR},{HidNpadButton_Plus,C::Plus},{HidNpadButton_Minus,C::Minus},{HidNpadButton_Up,C::Up},{HidNpadButton_Down,C::Down},{HidNpadButton_Left,C::Left},{HidNpadButton_Right,C::Right},{HidNpadButton_StickR,C::RStick},{HidNpadButton_StickL,C::LStick}};
        for(const auto& binding:map)if(buttons&binding.first)held|=binding.second;
        const auto left=padGetStickPos(&pad,0),right=padGetStickPos(&pad,1);const uint32_t now=SDL_GetTicks();
        const float dt=std::clamp((now-last_tick)/1000.0f,.001f,.05f);
        events=input.update({held,left.x/32767.0f,-left.y/32767.0f,right.x/32767.0f,-right.y/32767.0f,padIsConnected(&pad)},dt);last_tick=now;
        if(input.motion_enabled()!=motion_was_enabled){motion_was_enabled=input.motion_enabled();ds::startup_log("Motion aiming: %s",motion_was_enabled?"enabled (aim only)":"disabled");}
        if(input.motion_enabled()&&!input.is_menu()&&(held&(C::L|C::ZL))){const auto delta=sensors.read(padIsHandheld(&pad),padGetStyleSet(&pad),dt);if(delta.first||delta.second){ds::GuestInput e;e.kind=ds::GuestInput::Kind::look;e.x=delta.first;e.y=delta.second;events.push_back(e);}}
        HidTouchScreenState touch{};hidGetTouchScreenStates(&touch,1);
        if(touch.count||touched){ds::GuestInput e;e.kind=ds::GuestInput::Kind::pointer;e.module=1000;e.id=0;e.code=touch.count?(touched?0x4000c:0x6000c):0x8000c;if(touch.count){touch_x=touch.touches[0].x/1280.0f*640;touch_y=touch.touches[0].y/720.0f*480;}e.x=touch_x;e.y=touch_y;events.push_back(e);touched=touch.count!=0;}
        if(quit){ds::GuestInput e;e.kind=ds::GuestInput::Kind::quit;events.push_back(e);}
        for(auto& e:events)if(e.kind==ds::GuestInput::Kind::pointer){e.x*=drawable_width/640.0f;e.y*=drawable_height/480.0f;}
        return events;
    };
    graphics.present=[&]{
        if(starting){++loading_frames;draw_loading();}
        if(input.cursor_visible()){
            const auto resolve=[](const char* name){auto* p=SDL_GL_GetProcAddress(name);if(!p)throw std::runtime_error(std::string("Cursor GL unavailable: ")+name);return p;};
            auto get_int=reinterpret_cast<void(*)(unsigned,int*)>(resolve("glGetIntegerv"));
            auto get_float=reinterpret_cast<void(*)(unsigned,float*)>(resolve("glGetFloatv"));
            auto get_bool=reinterpret_cast<void(*)(unsigned,unsigned char*)>(resolve("glGetBooleanv"));
            auto is_enabled=reinterpret_cast<unsigned char(*)(unsigned)>(resolve("glIsEnabled"));
            auto enable=reinterpret_cast<void(*)(unsigned)>(resolve("glEnable"));auto disable=reinterpret_cast<void(*)(unsigned)>(resolve("glDisable"));
            auto scissor=reinterpret_cast<void(*)(int,int,int,int)>(resolve("glScissor"));auto color=reinterpret_cast<void(*)(float,float,float,float)>(resolve("glClearColor"));
            auto mask=reinterpret_cast<void(*)(unsigned char,unsigned char,unsigned char,unsigned char)>(resolve("glColorMask"));auto clear=reinterpret_cast<void(*)(unsigned)>(resolve("glClear"));
            int box[4]{};float saved[4]{};unsigned char channels[4]{};get_int(0x0c10,box);get_float(0x0c22,saved);get_bool(0x0c23,channels);const auto enabled=is_enabled(0x0c11);
            const int x=int(input.x()*drawable_width/640.0f),y=drawable_height-1-int(input.y()*drawable_height/480.0f);
            enable(0x0c11);mask(1,1,1,1);color(0,0,0,1);scissor(x-7,y-1,15,3);clear(0x4000);scissor(x-1,y-7,3,15);clear(0x4000);color(1,1,1,1);scissor(x-6,y,13,1);clear(0x4000);scissor(x,y-6,1,13);clear(0x4000);
            color(saved[0],saved[1],saved[2],saved[3]);mask(channels[0],channels[1],channels[2],channels[3]);scissor(box[0],box[1],box[2],box[3]);if(!enabled)disable(0x0c11);
        }
        SDL_GL_SwapWindow(window);
    };
#ifndef DS_GAME_LAUNCHER
    const std::string hd_root=root==ds::paths::packaged_game_directory?ds::paths::packaged_hd_directory:ds::paths::hd_directory;
    graphics.hd_texture=[hd_root](const std::string& key){return ds::read_hd_texture(hd_root,key);};
    log+="Optional HD root: "+hd_root+" (missing files use original textures)\n";
#endif
    log+=lifecycle_probe(root+"/lib/armeabi/libEAMGameDeadSpace.so",root,frames,std::move(graphics));
    auto get_error=reinterpret_cast<unsigned(*)()>(SDL_GL_GetProcAddress("glGetError"));
    if(get_error){char text[80];std::snprintf(text,sizeof(text),"Native context GL error after probe: 0x%x\n",get_error());log+=text;}
    SDL_CloseAudioDevice(audio);SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();return log;
}
#else
std::string game_graphics_probe(const std::string&,unsigned) {return "Native game graphics requires the CPU build.\n";}
#endif
