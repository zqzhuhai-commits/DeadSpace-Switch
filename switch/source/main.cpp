#include <switch.h>
#include "preflight.hpp"
#include "platform_probe.hpp"
#include "app_paths.hpp"
#ifdef DS_CPU_PROBE
#include "cpu_probe.hpp"
#include "constructor_probe.hpp"
#include "lifecycle_probe.hpp"
#endif
#include <cstdio>
#include <string>
#include <sys/stat.h>

#ifndef DS_APP_VERSION
#define DS_APP_VERSION "development"
#endif
static const char* root=ds::paths::game_directory;
static FILE* logfile=nullptr;
static void record(const std::string& s) {
    if (logfile) { std::fprintf(logfile,"%s\n",s.c_str()); std::fflush(logfile); }
}
static void draw_report() {
    consoleClear();
    std::puts("Dead Space Mobile " DS_APP_VERSION " - DEVELOPMENT DIAGNOSTICS\n");
    std::puts("Game lifecycle is NOT implemented. This is not a playable port.\n");
    const auto r=ds::preflight(root);
    std::printf("Data: %s\n",root);
    record("--- donor preflight ---");
    for (const auto& m:r.messages) { std::puts(m.c_str()); record(m); }
    std::puts("\nA: rescan  X: GPU/PVRTC/audio  Y: GLES1 compatibility  +: exit");
#ifdef DS_CPU_PROBE
    std::puts("B: ARM32 functions + game constructors (not gameplay)");
#endif
    std::puts("Touch and left/right stick values appear below.");
    std::printf("Log: %s\n",ds::paths::log_file);
    if (!logfile) std::puts("WARNING: log file could not be opened; check SD card space/access.");
    record(r.ready()?"Donor preflight passed; game lifecycle unavailable":"Donor preflight failed");
}
int main() {
    consoleInit(nullptr);
    mkdir(ds::paths::app_directory,0777);
    logfile=std::fopen(ds::paths::log_file,"a");
    record("Dead Space Mobile diagnostics " DS_APP_VERSION " start");
#ifdef DS_CPU_PROBE
    record("Build mode: Dynarmic CPU probe enabled");
#else
    record("Build mode: platform diagnostics only");
#endif
    const Result romfs_result=romfsInit();
    const bool romfs_mounted=R_SUCCEEDED(romfs_result);
    struct stat st{};
    if(stat("romfs:/gamedata/lib/armeabi/libEAMGameDeadSpace.so",&st)==0 && S_ISREG(st.st_mode))root=ds::paths::packaged_game_directory;
    record(std::string("Selected data root: ")+root);
    PadState pad;
    padConfigureInput(1,HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad); hidInitializeTouchScreen();
    draw_report();
    while (appletMainLoop()) {
        padUpdate(&pad);
        const u64 down=padGetButtonsDown(&pad);
        if (down&HidNpadButton_Plus) break;
        if (down&HidNpadButton_A) draw_report();
        if (down&HidNpadButton_X) {
            consoleExit(nullptr);
            const auto result=platform_probe(); record(result);
            consoleInit(nullptr); draw_report();
            std::puts(result.c_str());
        }
        if (down&HidNpadButton_Y) {
            consoleExit(nullptr);
            const auto result=fixed_function_probe(); record(result);
            consoleInit(nullptr); draw_report();
            std::puts(result.c_str());
        }
#ifdef DS_CPU_PROBE
        if (down&HidNpadButton_B) {
            record("ARM32 CPU probe begin");
            const auto library=std::string(root)+"/lib/armeabi/libEAMGameDeadSpace.so";
            const auto result=cpu_probe(library)+constructor_probe(library);
            record(result);consoleClear();std::puts(result.c_str());
            record("ARM32 CPU probe end");
            std::puts("A: return to data diagnostics  +: exit");
        }
        if (down&HidNpadButton_L) {
            record("JNI lifecycle probe begin");
            const auto library=std::string(root)+"/lib/armeabi/libEAMGameDeadSpace.so";
            const auto result=lifecycle_probe(library,root,2);
            record(result);consoleClear();std::puts(result.c_str());
            record("JNI lifecycle probe end");
            std::puts("A: return to data diagnostics  +: exit");
        }
#endif
        const auto left=padGetStickPos(&pad,0),right=padGetStickPos(&pad,1);
        HidTouchScreenState touch{};
        hidGetTouchScreenStates(&touch,1);
        std::printf("\x1b[32;1HInput L:%6d,%6d R:%6d,%6d touch:%u   \n",left.x,left.y,right.x,right.y,touch.count);
        if (touch.count) std::printf("Touch x=%u y=%u     ",touch.touches[0].x,touch.touches[0].y);
        if (down) { char b[64]; std::snprintf(b,sizeof(b),"Buttons down: %llx",static_cast<unsigned long long>(down)); record(b); }
        consoleUpdate(nullptr);
    }
    record("Clean shutdown");
    if (logfile) std::fclose(logfile);
    if (romfs_mounted) romfsExit();
    consoleExit(nullptr);
    return 0;
}
