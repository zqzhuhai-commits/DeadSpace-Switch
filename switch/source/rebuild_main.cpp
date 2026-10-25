#include <switch.h>
#include "app_paths.hpp"
#include "preflight.hpp"
#include "platform_probe.hpp"
#include "startup_log.hpp"
#include "render_test.hpp"
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <limits>
#include <exception>

extern "C" void userAppInit(void){ds::startup_begin(DS_APP_VERSION);}

int main(){
    ds::startup_log("BEGIN: main; version=%s; env=%s; HOS=0x%08x",DS_APP_VERSION,envIsNso()?"NSO":"NRO",unsigned(hosversionGet()));
    ds::startup_log("BEGIN: consoleInit");
    consoleInit(nullptr);mkdir("sdmc:/switch",0777);mkdir(ds::paths::app_directory,0777);
    auto* log=std::fopen(ds::paths::log_file,"a");
    auto record=[&](const std::string& text){ds::startup_log("%s",text.c_str());if(log){std::fprintf(log,"%s\n",text.c_str());std::fflush(log);}};
    ds::startup_log("END: consoleInit; BEGIN: romfsInit");
    const Result mount_result=romfsInit();const bool mounted=R_SUCCEEDED(mount_result);
    ds::startup_log("END: romfsInit result=0x%08x",unsigned(mount_result));
    const char* root=ds::paths::game_directory;struct stat st{};
    if(stat("romfs:/gamedata/lib/armeabi/libEAMGameDeadSpace.so",&st)==0)root=ds::paths::packaged_game_directory;
    record("Dead Space Mobile rebuild: actual game startup");record(std::string("Data root: ")+root);
    std::printf("Dead Space Mobile %s\nLoading game data...\n",DS_APP_VERSION);consoleUpdate(nullptr);
    ds::startup_log("BEGIN: preflight");
    const auto check=ds::preflight(root);
    for(const auto& message:check.messages)record(message);
    ds::startup_log("END: preflight ready=%u",unsigned(check.ready()));
    bool ran=false;
    if(check.ready()){
        const unsigned render_mode=ds::read_render_test_mode();
        std::printf("\nROOM-TEST-1 | build %s\n%s\n",DS_APP_VERSION,ds::render_test_name(render_mode));
        float lighting=.25f;
        if(auto* cfg=std::fopen("sdmc:/switch/deadspacemobile/lighting.cfg","r")){
            char extra=0;float value=0;
            if(std::fscanf(cfg,"%f %c",&value,&extra)==1&&value>=0&&value<=1)lighting=value;
            std::fclose(cfg);
        }
        std::printf("Lighting strength: %.2f | GPU buffers: ON\n",double(lighting));
        record(std::string("ROOM-TEST-1 mode=")+std::to_string(render_mode)+" lighting="+std::to_string(lighting));
        consoleUpdate(nullptr);svcSleepThread(3000000000LL);
        consoleExit(nullptr);ds::startup_log("BEGIN: game_graphics_probe");
        try{const auto result=game_graphics_probe(root,std::numeric_limits<unsigned>::max());record(result);}
        catch(const std::exception& error){record(std::string("STARTUP EXCEPTION: ")+error.what());}
        catch(...){record("STARTUP EXCEPTION: unknown");}
        consoleInit(nullptr);std::puts("Game session ended.\nDetails saved to diagnostics.log.");ran=true;
    }else std::puts("Game data is missing or unsupported.\nDetails saved to diagnostics.log.");
    std::puts("\nPress + to close.");
    PadState pad;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
    while(appletMainLoop()){padUpdate(&pad);if(padGetButtonsDown(&pad)&HidNpadButton_Plus)break;consoleUpdate(nullptr);}
    record(ran?"Rebuild session closed":"Data check failed");if(log)std::fclose(log);if(mounted)romfsExit();consoleExit(nullptr);return 0;
}
