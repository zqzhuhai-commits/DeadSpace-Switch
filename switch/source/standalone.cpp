#include <switch.h>
#include "app_paths.hpp"
#include "platform_probe.hpp"
#include "preflight.hpp"
#include <cstdio>
#include <string>
#include <sys/stat.h>

// Independent application entry: only packaged resources are eligible.
int main() {
    mkdir("sdmc:/switch",0777);
    mkdir(ds::paths::app_directory,0777);
    FILE* log=std::fopen(ds::paths::log_file,"a");
    consoleInit(nullptr);
    const bool mounted=R_SUCCEEDED(romfsInit());
    std::string result;
    if(!mounted)result="BLOCKED: cannot mount packaged RomFS.\n";
    else {
        const auto check=ds::preflight(ds::paths::packaged_game_directory);
        if(!check.ready()) {
            result="BLOCKED: packaged game data failed preflight.\n";
            for(const auto& message:check.messages)result+=message+"\n";
        } else {
            consoleExit(nullptr);
            result=game_graphics_probe(ds::paths::packaged_game_directory,true);
            consoleInit(nullptr);
        }
    }
    if(log){std::fprintf(log,"Standalone development session\n%s\n",result.c_str());std::fclose(log);}
    // Keep a failed startup visible until the user dismisses it.
    consoleClear();std::puts(result.c_str());std::puts("+ : exit");
    PadState pad;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
    while(appletMainLoop()) {
        padUpdate(&pad);if(padGetButtonsDown(&pad)&HidNpadButton_Plus)break;
        consoleUpdate(nullptr);
    }
    if(mounted)romfsExit();
    consoleExit(nullptr);
    return result.rfind("PASS:",0)==0?0:1;
}
