#include <switch.h>
#include "app_paths.hpp"
#include "boot_log.hpp"
#include "platform_probe.hpp"
#include "preflight.hpp"
#include <cstdio>
#include <string>
#include <sys/stat.h>

// Independent application entry: only packaged resources are eligible.
int main() {
    mkdir("sdmc:/switch",0777);
    mkdir(ds::paths::app_directory,0777);
    const bool logging=ds::boot_log(ds::paths::log_file,"\nBOOT: standalone main entered; checkpoint logging enabled; version=" DS_APP_VERSION "");
    ds::boot_log(ds::paths::log_file,"BEGIN: consoleInit");
    consoleInit(nullptr);
    std::puts("Dead Space Mobile - startup diagnostics");
    if(!logging)std::puts("WARNING: cannot write SD diagnostic log");
    std::puts("Mounting packaged resources...");consoleUpdate(nullptr);
    ds::boot_log(ds::paths::log_file,"BEGIN: romfsInit");
    const Result mount_result=romfsInit();
    const bool mounted=R_SUCCEEDED(mount_result);
    char mount_status[96];std::snprintf(mount_status,sizeof(mount_status),"END: romfsInit result=0x%08x",unsigned(mount_result));
    ds::boot_log(ds::paths::log_file,mount_status);
    std::string result;
    if(!mounted)result="BLOCKED: cannot mount packaged RomFS.\n";
    else {
        FILE* hd=std::fopen("romfs:/texture_hd/manifest.json","rb");
        ds::boot_log(ds::paths::log_file,hd?"HD: bundled manifest present (runtime matches still required)":"HD: no bundled manifest; original textures");
        if(hd)std::fclose(hd);
        ds::boot_log(ds::paths::log_file,"BEGIN: packaged preflight");
        const auto check=ds::preflight(ds::paths::packaged_game_directory);
        if(!check.ready()) {
            result="BLOCKED: packaged game data failed preflight.\n";
            for(const auto& message:check.messages)result+=message+"\n";
        } else {
            ds::boot_log(ds::paths::log_file,"END: packaged preflight OK; BEGIN: consoleExit");
            consoleExit(nullptr);
            result=game_graphics_probe(ds::paths::packaged_game_directory,true);
            consoleInit(nullptr);
        }
    }
    ds::boot_log(ds::paths::log_file,"SESSION RETURNED:\n"+result);
    if(result.find("SESSION: requested-exit\nPASS:")!=std::string::npos&&result.find("BLOCKED")==std::string::npos){
        if(mounted)romfsExit();
        consoleExit(nullptr);
        return 0;
    }
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
