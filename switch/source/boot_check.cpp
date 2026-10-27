#include <switch.h>
#include <cstdio>
#include <sys/stat.h>
namespace {
constexpr const char* log_path="sdmc:/switch/deadspacemobile/bootcheck-latest.log";
void record(const char* line){if(auto* f=std::fopen(log_path,"a")){std::fprintf(f,"%s\n",line);std::fclose(f);}}
}
extern "C" void userAppInit(void){
    mkdir("sdmc:/switch",0777);mkdir("sdmc:/switch/deadspacemobile",0777);
    if(auto* f=std::fopen(log_path,"w")){std::fputs("BOOT CHECK 0.4.2: libnx services and SD setup completed\n",f);std::fclose(f);}
}
int main(){
    record("BEGIN: main; consoleInit");consoleInit(nullptr);record("END: consoleInit");
    std::puts("Dead Space - BOOT CHECK 0.4.2\n\nBOOT CHECK PASSED\n\nApplication entry and basic services are running.\nThis check does not start the game.\n");
    std::printf("HOS: 0x%08x\n",unsigned(hosversionGet()));
    auto* f=std::fopen(log_path,"a");std::printf("SD log: %s\n",f?"writable":"UNAVAILABLE");if(f)std::fclose(f);
    std::puts("\nPress + to close.");
    PadState pad;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
    while(appletMainLoop()){padUpdate(&pad);if(padGetButtonsDown(&pad)&HidNpadButton_Plus)break;consoleUpdate(nullptr);}
    record("END: boot check closed");consoleExit(nullptr);return 0;
}
