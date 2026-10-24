#include <switch.h>
#include "app_paths.hpp"
#include "preflight.hpp"
#include "platform_probe.hpp"
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <limits>

int main(){
    consoleInit(nullptr);mkdir(ds::paths::app_directory,0777);
    auto* log=std::fopen(ds::paths::log_file,"a");
    auto record=[&](const std::string& text){if(log){std::fprintf(log,"%s\n",text.c_str());std::fflush(log);}};
    const bool mounted=R_SUCCEEDED(romfsInit());
    const char* root=ds::paths::game_directory;struct stat st{};
    if(stat("romfs:/gamedata/lib/armeabi/libEAMGameDeadSpace.so",&st)==0)root=ds::paths::packaged_game_directory;
    record("Dead Space Mobile rebuild: actual game startup");record(std::string("Data root: ")+root);
    std::puts("Dead Space Mobile - rebuild test\nLoading game data...\n");
    const auto check=ds::preflight(root);
    for(const auto& message:check.messages)record(message);
    bool ran=false;
    if(check.ready()){
        consoleExit(nullptr);const auto result=game_graphics_probe(root,std::numeric_limits<unsigned>::max());record(result);
        consoleInit(nullptr);std::puts("Game session ended.\nDetails saved to diagnostics.log.");ran=true;
    }else std::puts("Game data is missing or unsupported.\nDetails saved to diagnostics.log.");
    std::puts("\nPress + to close.");
    PadState pad;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
    while(appletMainLoop()){padUpdate(&pad);if(padGetButtonsDown(&pad)&HidNpadButton_Plus)break;consoleUpdate(nullptr);}
    record(ran?"Rebuild session closed":"Data check failed");if(log)std::fclose(log);if(mounted)romfsExit();consoleExit(nullptr);return 0;
}
