#include <switch.h>
#include "preflight.hpp"
#include "platform_probe.hpp"
#ifdef DS_CPU_PROBE
#include "cpu_probe.hpp"
#endif
#include <cstdio>
#include <string>
#include <sys/stat.h>

static const char* root="sdmc:/switch/deadspace/gamedata";
static FILE* logfile=nullptr;
static void record(const std::string& s) {
    if (logfile) { std::fprintf(logfile,"%s\n",s.c_str()); std::fflush(logfile); }
}
static void draw_report() {
    consoleClear();
    std::puts("DEAD SPACE SWITCH - DEVELOPMENT DIAGNOSTICS\n");
    std::puts("Game lifecycle is NOT implemented. This is not a playable port.\n");
    const auto r=ds::preflight(root);
    record("--- donor preflight ---");
    for (const auto& m:r.messages) { std::puts(m.c_str()); record(m); }
    std::puts("\nA: rescan  X: GPU/PVRTC/audio  Y: GLES1 compatibility  +: exit");
#ifdef DS_CPU_PROBE
    std::puts("B: ARM32 Dynarmic leaf-function probe (not game startup)");
#endif
    std::puts("Touch and left/right stick values appear below.");
    std::puts("Log: /switch/deadspace/diagnostics.log");
    if (!logfile) std::puts("WARNING: log file could not be opened; check SD card space/access.");
    record(r.ready()?"Donor preflight passed; execution unavailable":"Donor preflight failed");
}
int main() {
    consoleInit(nullptr);
    mkdir("sdmc:/switch/deadspace",0777);
    logfile=std::fopen("sdmc:/switch/deadspace/diagnostics.log","a");
    record("Dead Space Switch diagnostics 0.1.0 start");
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
            const auto result=cpu_probe(std::string(root)+"/lib/armeabi/libEAMGameDeadSpace.so");
            record(result);consoleClear();std::puts(result.c_str());
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
    consoleExit(nullptr);
    return 0;
}
