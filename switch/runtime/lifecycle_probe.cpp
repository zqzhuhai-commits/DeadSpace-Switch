// 0.3.1 lifecycle probe: bounded ARM32 JNI/JavaVM migration from
// switch/tools/boot_probe.py into the Switch C++ runtime.
// Guest addresses stay 32-bit integers; host services are dispatched by SVC id.
#include "lifecycle_probe.hpp"
#include "guest_image.hpp"
#include "guest_call_scope.hpp"
#include "guest_heap.hpp"
#include "guest_threads.hpp"
#include "guest_jump.hpp"
#include "guest_files.hpp"
#include "guest_storage.hpp"
#include "guest_arena.hpp"
#include "guest_jni.hpp"
#include "jni_variant_bindings.hpp"
#include "jni_table_indices.hpp"
#include <dynarmic/interface/A32/a32.h>
#include <dynarmic/interface/A32/config.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cstdlib>
#include <cerrno>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <thread>

namespace {
constexpr uint32_t heap=ds::GuestLayout::heap_base, arena=0x40000000, arena_end=0x50000000;
constexpr uint32_t stack=ds::GuestLayout::stack_base, thunks=0x60000000, stop=0x70000000;
constexpr unsigned jni_slots=233, vm_slots=8;

using ds::MethodKey;

class LifecycleRuntime final: public Dynarmic::A32::UserCallbacks, public ds::GuestJni {
    using Page=std::array<uint8_t,4096>;
    std::unordered_map<uint32_t,Page> pages;
    using FastTable=std::array<uint8_t*,1u<<20>;
    std::unique_ptr<FastTable> fast_pages=std::make_unique<FastTable>();
    std::vector<std::string> functions;
    std::map<uint32_t,uint32_t> extra_code;
    ds::GuestHeap guest_heap;
    struct Mutex {unsigned type=0,depth=0;uint32_t owner=0;};
    std::map<uint32_t,Mutex> mutexes;
    std::map<uint32_t,unsigned> attrs,semaphores;
    std::map<uint32_t,uint32_t> tls;
    ds::GuestThreads threads;
    ds::GuestJumps jumps;
    struct ThreadAttr {uint32_t stack_size=1024*1024,detached=0;};
    std::map<uint32_t,ThreadAttr> thread_attrs;
    unsigned call_depth=0,thread_switches=0,threads_created=0;uint64_t quantum=0;
    uint32_t next_tls_key=1;
    bool rotate=false;
    uint32_t current_thread() const override {return threads.current();}
    uint32_t guest_pc() const override {return cpu?cpu->Regs()[14]:0;}
    void block_monitor(uint32_t ref) override {if(call_depth!=1)throw std::runtime_error("JNI monitor wait across host callback boundary");threads.block(ds::GuestThreads::State::monitor,ref);rotate=true;}
    uint32_t monitor_waiter(uint32_t ref) const override {return threads.waiter(ds::GuestThreads::State::monitor,ref);}
    void wake_monitor(uint32_t id) override {threads.wake(id);}
    std::vector<uint32_t> destructors;
    ds::GuestFiles files;
    std::unique_ptr<ds::GuestStorage> storage;
    std::vector<std::string> trace;
    std::set<MethodKey> traced_methods;
    std::string data_root;
    ds::GuestArena arena_memory;
    void discard_page(uint32_t page){(*fast_pages)[page]=nullptr;pages.erase(page);}
    bool fast_writable(uint32_t base)const{
        const uint64_t end=uint64_t(base)+4096;
        return image.writable(base,4096)||
            (base>=stack&&end<=uint64_t(stack)+ds::GuestLayout::stack_size)||
            (base>=0x21000000u&&base<0x25000000u&&threads.mapped(base)&&threads.mapped(base+4095))||
            (base>=heap&&end<=guest_heap.high_water())||
            (arena_memory.contains(base)&&arena_memory.contains(base+4095));
    }
    uint32_t pending=0,errno_address=0,env_addr=0,vm_addr=0,jni_table=0,vm_table=0;
    uint32_t ext_string=0;
    bool returned=false;
    uint64_t budget=0;
    uint64_t realtime_origin=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    std::string fault;
    unsigned constructors_completed=0,service_calls=0,jni_calls=0,gl_calls=0,frames_done=0,strstr_log=0,asset_reads=0,guard_log=0,svc_log=0,call277efc=0,call28778=0;std::string last_service;
    std::map<std::string,uint64_t> service_counts;
    ds::GraphicsBackend graphics;
    std::unique_ptr<ds::GuestGl> native_gl;
    std::map<uint32_t,std::vector<int16_t>> pending_audio;
    uint64_t audio_samples=0;
    std::chrono::steady_clock::time_point real_start=std::chrono::steady_clock::now();
    void update_clock(){if(graphics.realtime_clock){const auto elapsed=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-real_start).count());if(elapsed>threads.time())threads.tick(elapsed-threads.time());}}

    void pump_audio() {
        for(auto it=pending_audio.begin();it!=pending_audio.end();) {
            auto* task=threads.find(it->first);
            if(!task)throw std::runtime_error("Audio writer thread disappeared");
            const auto written=graphics.audio_write(it->second);
            if(written){
                if(*written>it->second.size())throw std::runtime_error("Audio backend sample count invalid");
                task->cpu.regs[0]=*written;audio_samples+=*written;threads.wake(it->first);it=pending_audio.erase(it);
            }else ++it;
        }
    }

    bool mapped(uint32_t a) const {
        return threads.mapped(a) || (a>=heap && a<guest_heap.high_water()) ||
               arena_memory.contains(a) ||
               (a>=0x10000000u && a<0x20200000u);
    }
    void fail(const std::string& message) {
        if(fault.empty()){
            std::string ctx=message;
            if(cpu){ctx+=" pc="+to_hex(cpu->Regs()[15])+" lr="+to_hex(cpu->Regs()[14])+" brk="+to_hex(arena_memory.program_break())+" first_map="+to_hex(arena_memory.lowest_mapping());for(int i=0;i<13;++i)ctx+=" r"+std::to_string(i)+"="+to_hex(cpu->Regs()[i]);}
            fault=ctx;
        }
        if(cpu)cpu->HaltExecution();
        budget=0;
    }
    uint32_t alloc(uint32_t size) override {return guest_heap.allocate(size);}
    uint32_t arg(unsigned n) override {return n<4?cpu->Regs()[n]:MemoryRead32(cpu->Regs()[13]+4*(n-4));}
    void bounded(uint32_t n) {if(n>64*1024*1024)throw std::runtime_error("Memory operation limit");}
    std::string guest_text(uint32_t p) {
        if(!p){std::string e="Null guest string pc="+to_hex(cpu->Regs()[15])+" lr="+to_hex(cpu->Regs()[14])+" r1="+to_hex(cpu->Regs()[1]);throw std::runtime_error(e);}
        std::string s;for(uint32_t i=0;i<1024*1024;++i){uint8_t b=MemoryRead8(p+i);if(!b)return s;s+=char(b);}throw std::runtime_error("Unterminated guest string");
    }
    uint32_t guest_string(const std::string& v) {
        const auto p=alloc(uint32_t(v.size()+1));
        for(uint32_t i=0;i<v.size();++i)MemoryWrite8(p+i,uint8_t(v[i]));
        MemoryWrite8(p+uint32_t(v.size()),0);
        return p;
    }
    std::string resolve_path(const std::string& raw,bool writing=false) {
        if(!storage)throw std::runtime_error("Storage not initialized");
        return storage->resolve(raw,writing);
    }
    uint64_t java_delegate(uint32_t obj,const MethodKey& m,const std::vector<uint64_t>& values) override {
        ++jni_calls;
        const std::string& cls=objects[obj].cls;
        if(traced_methods.find(m)==traced_methods.end()){traced_methods.insert(m);trace.push_back("Java "+cls+"."+m.name+m.sig);}
        if(m.name=="<init>" && m.sig=="()V") {
            // These upstream delegate constructors only record configuration;
            // unlike a generic constructor stub, unknown classes still fail.
            static const std::array<const char*,6> stateless={
                "com/ea/blast/SystemAndroidDelegate", "com/ea/blast/DisplayAndroidDelegate",
                "com/ea/blast/GetAppDataDirectoryDelegate", "com/ea/blast/PowerManagerAndroid",
                "com/ea/blast/DeviceOrientationHandlerAndroidDelegate", "com/ea/blast/AccelerometerAndroidDelegate"};
            if(std::find(stateless.begin(),stateless.end(),cls)!=stateless.end())return 0;
        }
        if(cls=="com/ea/blast/MainActivity"){
            if(m.name=="GetInstance"){const uint32_t r=singleton("com/ea/blast/MainActivity");trace.push_back("  GetInstance -> "+to_hex(r));return r;}
            if(m.name=="getAssets"){const uint32_t r=singleton("android/content/res/AssetManager");trace.push_back("  getAssets -> "+to_hex(r));return r;}
        }
        if(cls=="android/media/AudioTrack") {
            if(m.name=="play"||m.name=="stop") {
                if(!graphics.audio_playing)throw std::runtime_error("Audio playback backend unavailable");
                graphics.audio_playing(m.name=="play");return 0;
            }
            if(m.name=="write"&&m.sig=="([SII)I"){
                if(!graphics.audio_write)throw std::runtime_error("Audio PCM backend unavailable");
                const auto& array=object(uint32_t(values.at(0)));
                const int32_t offset=int32_t(values.at(1)),length=int32_t(values.at(2));
                if(array.cls!="[S"||offset<0||length<0||uint64_t(offset)+uint32_t(length)>array.length)return uint32_t(-2);
                if(uint32_t(length)>1024*1024)throw std::runtime_error("Audio write sample bound");
                std::vector<int16_t> pcm(static_cast<size_t>(length));
                for(uint32_t i=0;i<uint32_t(length);++i)pcm[i]=int16_t(MemoryRead16(array.elements.at(0)+2*(uint32_t(offset)+i)));
                const auto sent=graphics.audio_write(pcm);
                if(sent){if(*sent>pcm.size())throw std::runtime_error("Audio backend sample count invalid");audio_samples+=*sent;return *sent;}
                if(call_depth!=1)throw std::runtime_error("Blocking audio write across host callback boundary");
                pending_audio[threads.current()]=std::move(pcm);threads.block(ds::GuestThreads::State::audio,0);rotate=true;return 0;
            }
        }
        if(const auto result=asset_delegate(obj,m,values,data_root))return *result;
        if(cls=="com/ea/EAIO/EAIO"&&m.name=="Startup"){
            const auto it=image.exports.find("Java_com_ea_EAIO_EAIO_Startup");
            if(it==image.exports.end())throw std::runtime_error("EAIO Startup export missing");
            const uint32_t am=uint32_t(values.at(0));
            if(object(am).cls!="android/content/res/AssetManager")throw std::runtime_error("EAIO Startup requires AssetManager");
            return guest_call(it->second,{env_addr,0,am},"EAIO_Startup");
        }
        if(cls=="com/ea/blast/SystemAndroidDelegate"){
            static const std::map<std::string,std::string> props={
              {"GetAccelerometerCount","1"},{"IsBatteryStateAvailable","0"},{"GetCameraCount","0"},
              {"GetChipset","0"},{"GetCompassCount","0"},{"GetManufacturer","sony"},
              {"GetDeviceModel","Switch"},{"GetDeviceName","Switch"},{"GetPhoneNumber","0"},
              {"GetDeviceUniqueId","-1"},{"GetDisplayCount","1"},{"GetGyroscopeCount","1"},
              {"GetLocationAvailable","true"},{"GetMicrophoneCount","1"},{"GetApiLevel","19"},
              {"GetPlatformRawName","Android"},{"GetPlatformStdName","Android"},{"GetPlatformVersion","4.4.4"},
              {"GetPhysicalKeyboardCount","1"},{"GetProcessorArchitecture","armeabi-v7a"},
              {"GetLanguage","zh"},{"GetLocale","zh"},{"GetTotalRAM","-1"},{"GetTouchPadCount","1"},
              {"GetTouchScreenCount","1"},{"GetTrackBallCount","0"},{"GetVibratorCount","0"},
              {"GetVirtualKeyboardCount","1"}};
            const auto it=props.find(m.name);if(it!=props.end())return java_string(it->second);
        }
        if(cls=="com/ea/blast/GetAppDataDirectoryDelegate"){
            if(m.name=="GetAppDataDirectory"||m.name=="GetExternalStorageDirectory")return java_string("/data");
        }
        if(cls=="com/ea/blast/DisplayAndroidDelegate"){
            if(m.name=="GetDefaultWidth")return graphics.surface_height;
            if(m.name=="GetDefaultHeight")return graphics.surface_width;
            if(m.name=="GetStdOrientation")return 0;
            if(m.name=="GetDpiX"||m.name=="GetDpiY"){const float v=229.0f;uint32_t bits=0;std::memcpy(&bits,&v,4);return bits;}
            if(m.name=="SetStdOrientation")return 0;
        }
        if(cls=="com/ea/blast/PowerManagerAndroid"&&m.name=="ApplyKeepAwake"){trace.push_back("keep-awake request recorded only");return 0;}
        if(cls=="com/eamobile/Query"){
            if(m.name=="isContentReady"){std::FILE* fp=std::fopen((data_root+"/assets/EAMCore.ini").c_str(),"rb");if(fp)std::fclose(fp);return fp?1:0;}
            if(m.name=="getVersion")return java_string("1.1.33");
            if(m.name=="getTotalMemory")return 256;
        }
        if(cls=="com/ea/blast/TouchSurfaceAndroid"&&m.name=="IsTouchScreenMultiTouch")return 0;
        if(cls=="com/ea/blast/DeviceOrientationHandlerAndroidDelegate"){
            if(m.name=="OnLifeCycleFocusGained"||m.name=="OnLifeCycleFocusLost"||m.name=="SetEnabled")return 0;
        }
        if(cls=="com/ea/blast/AccelerometerAndroidDelegate"||cls=="com/ea/blast/GyroscopeAndroidDelegate"){
            if(m.name=="SetEnabled"||m.name=="SetUpdateFrequency"){trace.push_back("sensor configuration recorded only");return 0;}
        }
        throw std::runtime_error("Unsupported Java "+cls+"."+m.name+m.sig);
    }

    uint32_t guest_call(uint32_t address,const std::vector<uint32_t>& args,const std::string& label,bool top_level=false) {
        struct Depth {unsigned& d;Depth(unsigned& v):d(v){++d;}~Depth(){--d;}} depth(call_depth);
        struct Quantum {uint64_t& q;uint64_t old;Quantum(uint64_t& v):q(v),old(v){}~Quantum(){q=old;}} saved_quantum(quantum);
        const bool outer=call_depth==1;
        if(outer)jumps.clear_owner(threads.current());
        ds::GuestCallScope scope(*cpu,returned,pending,budget,fault);
        const uint32_t call_sp=ds::guest_call_sp(cpu->Regs()[13],unsigned(args.size()),top_level,threads.thread().stack_begin,threads.thread().stack_size);
        cpu->Reset();if(!top_level)cpu->Regs().fill(0);
        cpu->Regs()[0]=args.size()>0?args[0]:0;cpu->Regs()[1]=args.size()>1?args[1]:0;
        cpu->Regs()[2]=args.size()>2?args[2]:0;cpu->Regs()[3]=args.size()>3?args[3]:0;
        cpu->Regs()[13]=call_sp;
        cpu->Regs()[14]=stop;cpu->Regs()[15]=address&~1u;cpu->SetCpsr((address&1)?0x30:0x10);cpu->SetFpscr(0);
        for(unsigned i=4;i<args.size();++i)MemoryWrite32(cpu->Regs()[13]+4*(i-4),args[i]);
        budget=200000000;returned=false;pending=0;
        auto last_progress=std::chrono::steady_clock::now();
        const auto deadline=last_progress+std::chrono::seconds(120);
        unsigned calls=0;
        unsigned beats=0;
        bool background=false;std::chrono::steady_clock::time_point background_deadline;
        while(!returned&&fault.empty()&&budget){
            if(background&&std::chrono::steady_clock::now()>=background_deadline)break;
            update_clock();
            const auto& active=threads.thread();
            const uint64_t sp=cpu->Regs()[13];
            if(sp<active.stack_begin||sp>uint64_t(active.stack_begin)+active.stack_size)
                throw std::runtime_error(label+": guest stack escaped its allocation; sp=0x"+to_hex(uint32_t(sp))+" pc=0x"+to_hex(cpu->Regs()[15]));
            if((++beats&1023)==0){
                const auto now=std::chrono::steady_clock::now();
                if(now>=deadline)throw std::runtime_error(label+": diagnostic wall-clock budget exhausted");
                if(graphics.record&&now-last_progress>=std::chrono::seconds(5)){
                    graphics.record("WORK: "+label+" thread="+std::to_string(threads.current())+" imports="+std::to_string(calls)+" GL="+std::to_string(gl_calls)+" assets="+std::to_string(asset_read_bytes));last_progress=now;
                }
            }
            quantum=std::min<uint64_t>(budget,100000);
            cpu->Run();
            if(pending){const auto id=pending;pending=0;if(id>functions.size()||++calls>50000000)throw std::runtime_error("Import dispatch limit "+label);++service_calls;if(cpu->Regs()[15]>=0x10235800u&&cpu->Regs()[15]<0x10235960u&&++svc_log<=40)trace.push_back("  SVC "+functions[id-1]+" guest_pc=0x"+to_hex(cpu->Regs()[15]-8)+" a=0x"+to_hex(cpu->Regs()[0])+" b=0x"+to_hex(cpu->Regs()[1])+" c=0x"+to_hex(cpu->Regs()[2])+" d=0x"+to_hex(cpu->Regs()[3]));service(functions[id-1]);}
            if(!outer)continue;
            if(!fault.empty())break;
            if(returned) {
                if(threads.current()==1){
                    if(!graphics.realtime_clock||label.rfind("NativeOnDrawFrame[",0)!=0)break;
                    auto& main=threads.thread();main.cpu.save(*cpu);main.exception=jni_exception;main.error=errno_address;main.state=ds::GuestThreads::State::frame;
                    background=true;background_deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(8);returned=false;rotate=true;
                }else{
                trace.push_back("guest thread exited id="+std::to_string(threads.current()));
                // POSIX destructor passes run on the exiting worker's stack.
                for(unsigned pass=0;pass<4;++pass) {
                    std::vector<std::pair<uint32_t,uint32_t>> callbacks;
                    for(auto& kv:threads.thread().tls)if(kv.second&&tls.count(kv.first)&&tls.at(kv.first)){callbacks.emplace_back(tls.at(kv.first),kv.second);kv.second=0;}
                    if(callbacks.empty())break;
                    for(const auto& callback:callbacks)guest_call(callback.first,{callback.second},"pthread TLS destructor",false);
                }
                jumps.clear_owner(threads.current());
                const auto exiting=threads.thread();
                for(uint64_t at=exiting.stack_begin;at<uint64_t(exiting.stack_begin)+exiting.stack_size;at+=4096)(*fast_pages)[at>>12]=nullptr;
                threads.exit(cpu->Regs()[0]);returned=false;rotate=true;
                }
            }
            if(auto* old=threads.find(threads.current())) {
                old->cpu.save(*cpu);old->exception=jni_exception;old->error=errno_address;
            }
            threads.reap_joins([&](uint32_t p,uint32_t v){MemoryWrite32(p,v);});
            // Dispatch boundaries and instruction quanta are safe context-switch
            // points. Nested Java-to-native calls retain their suspended context.
            const auto previous=threads.current();
            pump_audio();
            bool background_idle=false;
            for(;;){
                update_clock();
                try{threads.select(!graphics.realtime_clock);break;}
                catch(const std::runtime_error&){
                    if(background&&(std::chrono::steady_clock::now()>=background_deadline||(!threads.has_timer()&&pending_audio.empty()))){background_idle=true;break;}
                    if(pending_audio.empty()&&!(graphics.realtime_clock&&threads.has_timer()))throw;
                    if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("Audio queue wait deadline exceeded");
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));pump_audio();
                }
            }
            if(background_idle)break;
            if(previous!=threads.current() || rotate) {
                ++thread_switches;cpu->ClearExclusiveState();threads.thread().cpu.load(*cpu);
                jni_exception=threads.thread().exception;errno_address=threads.thread().error;
            }
            if(threads.thread().timeout_error){MemoryWrite32(errno_address,threads.thread().timeout_error);threads.thread().timeout_error=0;}
            rotate=false;
        }
        if(background){
            if(auto* worker=threads.find(threads.current())){worker->cpu.save(*cpu);worker->exception=jni_exception;worker->error=errno_address;}
            threads.activate_main();cpu->ClearExclusiveState();threads.thread().cpu.load(*cpu);jni_exception=threads.thread().exception;errno_address=threads.thread().error;returned=true;
        }
        if(!fault.empty())throw std::runtime_error(label+": "+fault);
        if(!returned)throw std::runtime_error(label+": instruction budget exhausted");
        const uint32_t result=cpu->Regs()[0];
        if(label=="EAIO_Startup"||label=="NativeOnDrawFrame")trace.push_back("RET "+label+" pc=0x"+to_hex(cpu->Regs()[14]-8));
        if(top_level)scope.keep_top_level_result();
        return result;
    }

    void write_stat(uint32_t p,int64_t size,uint32_t mode=0x81a4){
        for(unsigned i=0;i<104;++i)MemoryWrite8(p+i,0);
        MemoryWrite64(p+0,0);MemoryWrite32(p+12,0);MemoryWrite32(p+16,mode);
        MemoryWrite32(p+20,1);MemoryWrite32(p+24,0);MemoryWrite32(p+28,0);
        MemoryWrite64(p+32,0);MemoryWrite64(p+48,uint64_t(size));
        MemoryWrite32(p+56,4096);MemoryWrite64(p+64,0);
        const uint32_t now=uint32_t(std::time(nullptr));
        MemoryWrite32(p+72,now);MemoryWrite32(p+76,0);
        MemoryWrite32(p+80,now);MemoryWrite32(p+84,0);
        MemoryWrite32(p+88,now);MemoryWrite32(p+92,0);
        MemoryWrite64(p+96,0);
    }

    void service(const std::string& name) {last_service=name;
        ++service_counts[name];
        const auto a=arg(0),b=arg(1),c=arg(2),d=arg(3);
        uint32_t result=0;
        static const std::array<const char*,15> float_imports={"acosf","asinf","atanf","atan2f","ceilf","cosf","expf","floorf","fmodf","logf","log10f","powf","sinf","sqrtf","tanf"};
        const bool floating=std::find(float_imports.begin(),float_imports.end(),name)!=float_imports.end();
        const std::string base=floating?name.substr(0,name.size()-1):name;
        if(name=="DS:mount_content_root"){
            guest_call(ds::GuestImage::base+0x30d6f4,{a,b,c,d},"mount original");
            const uint32_t begin=MemoryRead32(c),end=MemoryRead32(c+4);
            if(end<begin||(end-begin)%2||(end-begin)>512)throw std::runtime_error("Content mount string bounds");
            std::u16string source;for(uint32_t p=begin;p<end;p+=2)source+=char16_t(MemoryRead16(p));
            std::string source_debug;for(auto ch:source)source_debug+=ch<128?char(ch):'?';trace.push_back("Content mount source: "+source_debug);
            if(!source.empty()&&source.back()!=u'/')source+=u'/';source+=u"published";
            const std::u16string mount=u"/published";
            const auto source_bytes=alloc(uint32_t(source.size()*2+2)),mount_bytes=alloc(uint32_t(mount.size()*2+2));
            for(uint32_t i=0;i<source.size();++i)MemoryWrite16(source_bytes+2*i,uint16_t(source[i]));
            for(uint32_t i=0;i<mount.size();++i)MemoryWrite16(mount_bytes+2*i,uint16_t(mount[i]));
            MemoryWrite16(source_bytes+uint32_t(source.size()*2),0);MemoryWrite16(mount_bytes+uint32_t(mount.size()*2),0);
            const auto source_copy=alloc(20),mount_copy=alloc(20);
            for(unsigned i=0;i<20;i+=4){MemoryWrite32(source_copy+i,MemoryRead32(c+i));MemoryWrite32(mount_copy+i,MemoryRead32(d+i));}
            MemoryWrite32(source_copy,source_bytes);MemoryWrite32(source_copy+4,source_bytes+uint32_t(source.size()*2));
            MemoryWrite32(mount_copy,mount_bytes);MemoryWrite32(mount_copy+4,mount_bytes+uint32_t(mount.size()*2));
            guest_call(ds::GuestImage::base+0x30d6f4,{a,b,source_copy,mount_copy},"mount published");
            guest_heap.release(source_copy);guest_heap.release(mount_copy);guest_heap.release(source_bytes);guest_heap.release(mount_bytes);
            trace.push_back("Mounted actual extracted content at /published");
        }
        else if(base=="memset"){bounded(c);std::vector<uint8_t> bytes(c,uint8_t(b));MemoryWriteBlock(a,bytes.data(),c);result=a;}
        else if(base=="memcpy"||base=="memmove"){bounded(c);std::vector<uint8_t> bytes(c);MemoryReadBlock(b,bytes.data(),c);MemoryWriteBlock(a,bytes.data(),c);result=a;}
        else if(base=="memcmp"){bounded(c);int cmp=0;for(uint32_t i=0;i<c;++i){uint8_t x=MemoryRead8(a+i),y=MemoryRead8(b+i);if(x!=y){cmp=x>y?1:-1;break;}}result=cmp;}
        else if(base=="malloc")result=alloc(a);
        else if(base=="calloc")result=guest_heap.zeroed(a,b,[&](uint32_t p,uint32_t n){for(uint32_t i=0;i<n;++i)MemoryWrite8(p+i,0);});
        else if(base=="realloc")result=guest_heap.reallocate(a,b,[&](uint32_t dst,uint32_t src,uint32_t n){for(uint32_t i=0;i<n;++i)MemoryWrite8(dst+i,MemoryRead8(src+i));});
        else if(base=="free")guest_heap.release(a);
        else if(base=="qsort"){
            const uint64_t bytes=uint64_t(b)*c;
            if(bytes>64*1024*1024||uint64_t(a)+bytes>0x100000000ull)throw std::runtime_error("Guest qsort bounds");
            if(b>1&&c){
                if(!a||!image.executable(d&~1u,4))throw std::runtime_error("Invalid qsort array/comparator");
                std::vector<uint32_t> order(b);for(uint32_t i=0;i<b;++i)order[i]=i;
                std::stable_sort(order.begin(),order.end(),[&](uint32_t left,uint32_t right){return int32_t(guest_call(d,{a+left*c,a+right*c},"qsort comparator"))<0;});
                std::vector<uint8_t> original(static_cast<size_t>(bytes)),sorted(static_cast<size_t>(bytes));MemoryReadBlock(a,original.data(),original.size());
                for(uint32_t i=0;i<b;++i)std::memcpy(sorted.data()+uint64_t(i)*c,original.data()+uint64_t(order[i])*c,c);
                MemoryWriteBlock(a,sorted.data(),sorted.size());
            }
        }
        else if(base=="strtod"){
            const auto text=guest_text(a);char* end=nullptr;errno=0;const double value=std::strtod(text.c_str(),&end);
            if(b)MemoryWrite32(b,a+uint32_t(end-text.c_str()));if(errno)MemoryWrite32(errno_address,errno);
            uint64_t bits;std::memcpy(&bits,&value,8);cpu->Regs()[0]=uint32_t(bits);cpu->Regs()[1]=uint32_t(bits>>32);goto done;
        }
        else if(base=="strlen")result=uint32_t(guest_text(a).size());
        else if(base=="strcmp"||base=="strncmp"){std::string x=guest_text(a),y=guest_text(b);if(base=="strncmp"){x=x.substr(0,c);y=y.substr(0,c);}result=(x>y)-(x<y);}
        else if(base=="strcpy"||base=="strncpy"||base=="strcat"){std::string x=base=="strcat"?guest_text(a)+guest_text(b):guest_text(b);if(base=="strncpy")x=x.substr(0,c);for(uint32_t i=0;i<x.size();++i)MemoryWrite8(a+i,x[i]);MemoryWrite8(a+uint32_t(x.size()),0);result=a;}
        else if(base=="strchr"||base=="strrchr"){const std::string x=guest_text(a);const char ch=char(b&255);const auto p=base=="strrchr"?x.rfind(ch):x.find(ch);result=p==std::string::npos?0:a+uint32_t(p);}
        else if(base=="strstr"){if(!a||!b){trace.push_back("  STRSTR-NULL a="+to_hex(a)+" b="+to_hex(b)+" lr="+to_hex(cpu->Regs()[14]-8));result=0;}else{const std::string x=guest_text(a),n=guest_text(b);const auto p=x.find(n);result=p==std::string::npos?0:a+uint32_t(p);}}
        else if(base=="strdup")result=guest_string(guest_text(a));
        else if(base=="atoi"){const std::string t=guest_text(a);result=0;bool neg=false;size_t i=0;if(i<t.size()&&(t[i]=='-'||t[i]=='+')){neg=t[i]=='-';++i;}while(i<t.size()&&t[i]>='0'&&t[i]<='9'){result=result*10+uint32_t(t[i]-'0');++i;}if(neg)result=uint32_t(0)-result;}
        else if(base=="__errno")result=errno_address;
        else if(base=="setjmp"||base=="_setjmp"||base=="sigsetjmp") {
            if(base=="sigsetjmp"&&b)throw std::runtime_error("Guest signal masks unsupported");
            jumps.save(*cpu,a,threads.current(),call_depth,[&](uint32_t p,uint32_t v){MemoryWrite32(p,v);});
        }
        else if(base=="longjmp"||base=="_longjmp"||base=="siglongjmp") {
            jumps.restore(*cpu,a,b,threads.current(),call_depth,[&](uint32_t p){return MemoryRead32(p);});goto done;
        }
        else if(base=="sbrk"){
            const auto old=arena_memory.change_break(int32_t(a));
            if(!old){MemoryWrite32(errno_address,12);result=0xffffffff;}
            else {
                result=*old;
                if(int32_t(a)<0)for(uint64_t p=(uint64_t(arena_memory.program_break())+4095)&~uint64_t(4095);p<*old;p+=4096)discard_page(uint32_t(p)>>12);
            }
        }
        else if(base=="mmap"){
            if(a||!b||(d&0x20)==0)throw std::runtime_error("Unsupported guest mmap");
            const auto address=arena_memory.map(b);
            if(!address){MemoryWrite32(errno_address,12);result=0xffffffff;}else result=*address;
        }
        else if(base=="munmap"){
            std::vector<std::pair<uint32_t,uint32_t>> released;
            if(!arena_memory.unmap(a,b,released)){MemoryWrite32(errno_address,22);result=0xffffffff;}
            else for(const auto& range:released)for(uint64_t p=range.first;p<uint64_t(range.first)+range.second;p+=4096)discard_page(uint32_t(p)>>12);
        }
        else if(base=="getenv")result=0;
        else if(base=="__aeabi_atexit")destructors.insert(destructors.end(),{a,b,c});
        else if(base=="__cxa_guard_acquire")result=MemoryRead8(a)==0;
        else if(base=="__cxa_guard_release")MemoryWrite8(a,1);
        else if(base=="pthread_self")result=threads.current();
        else if(base=="pthread_mutexattr_init")attrs[a]=0;
        else if(base=="pthread_mutexattr_settype"){if(b>2||!attrs.count(a))throw std::runtime_error("Invalid mutex attribute");attrs[a]=b;}
        else if(base=="pthread_mutexattr_setpshared"){if(b||!attrs.count(a))throw std::runtime_error("Shared mutex unsupported");}
        else if(base=="pthread_mutexattr_destroy"){if(!attrs.erase(a))throw std::runtime_error("Unknown mutex attribute");}
        else if(base=="pthread_mutex_init"){if(b&&!attrs.count(b))throw std::runtime_error("Unknown mutex attribute");mutexes[a]={b?attrs.at(b):0,0};}
        else if(base=="pthread_mutex_lock"||base=="pthread_mutex_trylock") {
            auto& m=mutexes[a];
            if(!m.depth){m.owner=threads.current();m.depth=1;}
            else if(m.owner==threads.current()&&m.type==1)++m.depth;
            else if(base=="pthread_mutex_trylock")result=16;
            else if(m.owner==threads.current()&&m.type==2)result=35;
            else {if(call_depth!=1)throw std::runtime_error("Mutex wait across host callback boundary");threads.block(ds::GuestThreads::State::mutex,a);rotate=true;}
        }
        else if(base=="pthread_mutex_unlock") {
            auto& m=mutexes[a];
            if(!m.depth||m.owner!=threads.current())result=1;
            else if(!--m.depth){m.owner=0;const auto waiter=threads.waiter(ds::GuestThreads::State::mutex,a);if(waiter){m.owner=waiter;m.depth=1;threads.wake(waiter);}}
        }
        else if(base=="pthread_mutex_destroy") {auto it=mutexes.find(a);if(it!=mutexes.end()){if(it->second.depth)result=16;else mutexes.erase(it);}}
        else if(base=="pthread_attr_init")thread_attrs[a]={};
        else if(base=="pthread_attr_setstacksize") {if(!thread_attrs.count(a)||b<16384||b>0x200000)result=22;else thread_attrs.at(a).stack_size=b;}
        else if(base=="pthread_attr_setdetachstate") {if(!thread_attrs.count(a)||b>1)result=22;else thread_attrs.at(a).detached=b;}
        else if(base=="pthread_attr_getstacksize"||base=="pthread_attr_getdetachstate") {if(!thread_attrs.count(a)||!b)result=22;else MemoryWrite32(b,base=="pthread_attr_getstacksize"?thread_attrs.at(a).stack_size:thread_attrs.at(a).detached);}
        else if(base=="pthread_attr_destroy") {if(!thread_attrs.erase(a))result=22;}
        else if(base=="pthread_attr_setschedparam"){
            if(!thread_attrs.count(a)||!b)result=22;
            else if(int32_t(MemoryRead32(b))!=0)result=95; // no realtime priority support
        }
        else if(base=="pthread_getschedparam"){
            if(!threads.find(a))result=3;
            else if(!b||!c)result=22;
            else{MemoryWrite32(b,0);MemoryWrite32(c,0);}
        }
        else if(base=="pthread_setschedparam"){
            if(!threads.find(a))result=3;
            else if(!c)result=22;
            else if(b!=0||MemoryRead32(c)!=0)result=95;
        }
        else if(base=="pthread_create") {
            if(!a || (b&&!thread_attrs.count(b)) || !image.executable(c&~1u,4))result=22;
            else {
                const auto attr=b?thread_attrs.at(b):ThreadAttr{};
                const auto id=threads.create(c,d,attr.stack_size,attr.detached!=0,stop);++threads_created;
                auto& child=*threads.find(id);child.error=alloc(4);MemoryWrite32(child.error,0);
                // Stack slots may be reused after exit: discard their old backing.
                for(auto it=pages.begin();it!=pages.end();) {const uint64_t at=uint64_t(it->first)<<12;if(at>=child.stack_begin&&at<uint64_t(child.stack_begin)+child.stack_size){(*fast_pages)[it->first]=nullptr;it=pages.erase(it);}else ++it;}
                MemoryWrite32(a,id);
                trace.push_back("guest thread created id="+std::to_string(id)+" entry=0x"+to_hex(c));
            }
        }
        else if(base=="pthread_join") {if(call_depth!=1)throw std::runtime_error("Join across host callback boundary");result=threads.join(a,b);rotate=result==0;}
        else if(base=="pthread_detach")result=threads.detach(a);
        else if(base=="pthread_exit") {if(call_depth!=1||threads.current()==1)throw std::runtime_error("Unsupported main/nested pthread_exit");result=a;returned=true;}
        else if(base=="sem_init"){if(b||c>0x7fffffff)throw std::runtime_error("Unsupported semaphore attributes");semaphores[a]=c;}
        else if(base=="sem_destroy"){if(!semaphores.erase(a))throw std::runtime_error("Unknown semaphore");}
        else if(base=="sem_post"){auto& v=semaphores.at(a);const auto waiter=threads.waiter(ds::GuestThreads::State::semaphore,a);if(waiter)threads.wake(waiter);else{if(v==0x7fffffff)throw std::runtime_error("Semaphore overflow");++v;}}
        else if(base=="sem_wait"||base=="sem_trywait"||base=="sem_timedwait"){
            if(!semaphores.count(a)){MemoryWrite32(errno_address,22);result=0xffffffff;}
            else if(semaphores.at(a))--semaphores.at(a);
            else if(base=="sem_trywait"){MemoryWrite32(errno_address,11);result=0xffffffff;}
            else {
                if(call_depth!=1)throw std::runtime_error("Semaphore wait across host callback boundary");
                if(base=="sem_timedwait"){
                    const int32_t sec=int32_t(MemoryRead32(b)),ns=int32_t(MemoryRead32(b+4));
                    if(sec<0||ns<0||ns>=1000000000){MemoryWrite32(errno_address,22);result=0xffffffff;}
                    else {
                        const uint64_t absolute=uint64_t(sec)*1000000000+uint32_t(ns);
                        const uint64_t deadline=absolute>realtime_origin?absolute-realtime_origin:0;
                        if(deadline<=threads.time()){MemoryWrite32(errno_address,110);result=0xffffffff;}
                        else {threads.block_until(ds::GuestThreads::State::semaphore,a,deadline);rotate=true;}
                    }
                }else{threads.block(ds::GuestThreads::State::semaphore,a);rotate=true;}
            }
        }
        else if(base=="sem_getvalue")MemoryWrite32(b,semaphores[a]);
        else if(base=="pthread_key_create"){if(next_tls_key>=4096)result=11;else{const auto key=next_tls_key++;tls[key]=b;MemoryWrite32(a,key);}}
        else if(base=="pthread_getspecific")result=threads.thread().tls.count(a)?threads.thread().tls.at(a):0;
        else if(base=="pthread_setspecific"){if(!tls.count(a))result=22;else threads.thread().tls[a]=b;}
        else if(base=="pthread_key_delete") {if(!tls.erase(a))result=22;}
        else if(base=="usleep"||base=="sleep"||base=="nanosleep"||base=="sched_yield") {
            if(call_depth!=1)throw std::runtime_error("Sleep/yield across host callback boundary");
            uint64_t nanos=0;
            if(base=="usleep")nanos=uint64_t(a)*1000;
            else if(base=="sleep")nanos=uint64_t(a)*1000000000;
            else if(base=="nanosleep") {const int32_t sec=int32_t(MemoryRead32(a)),ns=int32_t(MemoryRead32(a+4));if(sec<0||ns<0||ns>=1000000000){MemoryWrite32(errno_address,22);result=0xffffffff;}else nanos=uint64_t(sec)*1000000000+uint32_t(ns);}
            if(!result&&nanos)threads.sleep(nanos);
            rotate=true;
        }
        else if(base=="__atomic_swap"){result=MemoryRead32(b);MemoryWrite32(b,a);}
        else if(base=="__atomic_inc"||base=="__atomic_dec"){result=MemoryRead32(a);MemoryWrite32(a,result+(base=="__atomic_inc"?1:-1));}
        else if(base=="__atomic_cmpxchg"){result=MemoryRead32(c)!=a;if(!result)MemoryWrite32(c,b);}
        else if(base=="clock")result=uint32_t(uint64_t(std::clock())*1000000/CLOCKS_PER_SEC);
        else if(base=="time"){result=uint32_t((realtime_origin+threads.time())/1000000000);if(a)MemoryWrite32(a,result);}
        else if(base=="gettimeofday"){const auto now=realtime_origin+threads.time();if(a){MemoryWrite32(a,uint32_t(now/1000000000));MemoryWrite32(a+4,uint32_t((now%1000000000)/1000));}if(b)MemoryWrite64(b,0);result=0;}
        else if(base=="clock_gettime"){if(service_counts[name]<=4)trace.push_back("clock_gettime id="+std::to_string(a));if(a>1){MemoryWrite32(errno_address,22);result=0xffffffff;}else{const auto ns=threads.time()+(a==0?realtime_origin:0);MemoryWrite32(b,uint32_t(ns/1000000000));MemoryWrite32(b+4,uint32_t(ns%1000000000));}}
        else if(base=="stat"||base=="lstat"){
            const std::string path=resolve_path(guest_text(a));
            struct stat metadata{};
#ifdef _WIN32
            const int rc=::stat(path.c_str(),&metadata);
#else
            const int rc=base=="lstat" ? ::lstat(path.c_str(),&metadata) : ::stat(path.c_str(),&metadata);
#endif
            if(rc){MemoryWrite32(errno_address,errno);result=0xffffffff;}
            else write_stat(b,metadata.st_size,uint32_t(metadata.st_mode));
        }
        else if(base=="fstat"){
            const auto token=files.stream_for_fd(a);const auto size=token?files.size(token):-1;
            if(size<0){MemoryWrite32(errno_address,files.error());result=0xffffffff;}
            else write_stat(b,size);
        }
        else if(base=="open"||base=="open64"){
            constexpr uint32_t allowed=3|64|128|512|1024|0x8000|0x80000;
            if((b&~allowed)||(b&3)==3){MemoryWrite32(errno_address,22);result=0xffffffff;}
            else {
                const bool writing=(b&3)||(b&(64|512|1024));
                const auto path=resolve_path(guest_text(a),writing);
                const bool exists=std::filesystem::exists(path);
                if((b&64)&&(b&128)&&exists){MemoryWrite32(errno_address,17);result=0xffffffff;}
                else if(!exists&&!(b&64)){MemoryWrite32(errno_address,2);result=0xffffffff;}
                else {
                    const char* mode=(b&1024)?"ab+":(b&512)||!exists?"wb+":writing?"rb+":"rb";
                    result=files.open_fd(path.c_str(),mode);if(files.error())MemoryWrite32(errno_address,files.error());
                }
            }
        }
        else if(base=="mkdir"){
            std::error_code error;const bool created=std::filesystem::create_directory(resolve_path(guest_text(a),true),error);
            if(!created){MemoryWrite32(errno_address,error?uint32_t(error.default_error_condition().value()):17);result=0xffffffff;}
        }
        else if(base=="close"){result=uint32_t(files.close_fd(a));if(files.error())MemoryWrite32(errno_address,files.error());}
        else if(base=="read"||base=="fread"){
            const bool stream=base=="fread";
            const uint64_t count=stream?uint64_t(b)*c:c;
            if(count>64*1024*1024)throw std::runtime_error("File read size bound");
            const uint32_t destination=stream?a:b;
            if(uint64_t(destination)+count>0x100000000ull)throw std::runtime_error("File read guest address wraps");
            if(!count)result=0;
            else {
                const auto token=stream?d:files.stream_for_fd(a);
                std::vector<uint8_t> bytes(static_cast<size_t>(count));
                const auto got=files.read(token,bytes.data(),bytes.size());
                for(size_t i=0;i<got;++i)MemoryWrite8(destination+uint32_t(i),bytes[i]);
                result=stream?uint32_t(got/b):(files.error()?0xffffffff:uint32_t(got));
                if(files.error())MemoryWrite32(errno_address,files.error());
            }
        }
        else if(base=="lseek"||base=="fseek"){
            const auto token=base=="fseek"?a:files.stream_for_fd(a);
            if(!token){MemoryWrite32(errno_address,9);result=0xffffffff;}
            else if(files.seek(token,int32_t(b),int(c))){MemoryWrite32(errno_address,files.error());result=0xffffffff;}
            else if(base=="lseek"){
                const auto position=files.tell(token);
                if(position<0||position>INT32_MAX){MemoryWrite32(errno_address,position<0?files.error():75);result=0xffffffff;}
                else result=uint32_t(position);
            }
        }
        else if(base=="fopen"){
            const auto mode=guest_text(b);
            static const std::array<const char*,15> modes={"r","rb","r+","r+b","rb+","w","wb","w+","w+b","wb+","a","ab","a+","a+b","ab+"};
            if(std::find(modes.begin(),modes.end(),mode)==modes.end()){MemoryWrite32(errno_address,22);result=0;}
            else {
                const bool writing=mode[0]!='r'||mode.find('+')!=std::string::npos;
                result=files.open_stream(resolve_path(guest_text(a),writing).c_str(),mode.c_str());
                if(files.error())MemoryWrite32(errno_address,files.error());
            }
        }
        else if(base=="write"||base=="fwrite"){
            const bool stream=base=="fwrite";const uint64_t count=stream?uint64_t(b)*c:c;
            const uint32_t source=stream?a:b;
            if(count>64*1024*1024||uint64_t(source)+count>0x100000000ull)throw std::runtime_error("File write bounds");
            if(!count)result=0;
            else {
                const auto token=stream?d:files.stream_for_fd(a);
                std::vector<uint8_t> bytes(static_cast<size_t>(count));
                for(size_t i=0;i<bytes.size();++i)bytes[i]=MemoryRead8(source+uint32_t(i));
                const auto sent=files.write(token,bytes.data(),bytes.size());
                result=stream?uint32_t(sent/b):(files.error()?0xffffffff:uint32_t(sent));
                if(files.error())MemoryWrite32(errno_address,files.error());
            }
        }
        else if(base=="ftell"){
            const auto position=files.tell(a);
            if(position<0||position>INT32_MAX){MemoryWrite32(errno_address,position<0?files.error():75);result=0xffffffff;}
            else result=uint32_t(position);
        }
        else if(base=="fclose"){result=uint32_t(files.close_stream(a));if(files.error())MemoryWrite32(errno_address,files.error());}
        else if(base=="fflush"){result=uint32_t(files.flush(a));if(files.error())MemoryWrite32(errno_address,files.error());}
        else if(name.rfind("JNI:",0)==0){
            ++jni_calls;
            const auto value=GuestJni::jni_dispatch(name.substr(4),env_addr,vm_addr);
            cpu->Regs()[0]=uint32_t(value);
            if(name.find("LongMethod")!=std::string::npos || name.find("DoubleMethod")!=std::string::npos)cpu->Regs()[1]=uint32_t(value>>32);
            goto done;
        }
        else if(name.rfind("VM:",0)==0){
            const std::string op=name.substr(3);
            if(op=="GetEnv"||op=="AttachCurrentThread"||op=="AttachCurrentThreadAsDaemon"){MemoryWrite32(b,env_addr);result=0;}
            else if(op=="DetachCurrentThread")result=0;
            else throw std::runtime_error("Unsupported VM: "+op);
        }
        else if(base=="acos"||base=="asin"||base=="atan"||base=="atan2"||base=="ceil"||base=="cos"||base=="exp"||base=="floor"||base=="fmod"||base=="log"||base=="log10"||base=="pow"||base=="sin"||base=="sqrt"||base=="tan"){
            const bool single=name==base+"f";
            if(single){
                float x=0,y=0;std::memcpy(&x,&a,4);if(base=="atan2"||base=="fmod"||base=="pow")std::memcpy(&y,&b,4);
                float r=base=="atan2"?std::atan2(x,y):base=="fmod"?std::fmod(x,y):base=="pow"?std::pow(x,y):base=="acos"?std::acos(x):base=="asin"?std::asin(x):base=="atan"?std::atan(x):base=="ceil"?std::ceil(x):base=="cos"?std::cos(x):base=="exp"?std::exp(x):base=="floor"?std::floor(x):base=="log"?std::log(x):base=="log10"?std::log10(x):base=="sin"?std::sin(x):base=="tan"?std::tan(x):std::sqrt(x);
                std::memcpy(&result,&r,4);
            } else {
                uint64_t vx=uint64_t(a)|uint64_t(b)<<32;double dx;std::memcpy(&dx,&vx,8);
                uint64_t vy=uint64_t(c)|uint64_t(d)<<32;double dy;std::memcpy(&dy,&vy,8);
                double r=base=="atan2"?std::atan2(dx,dy):base=="fmod"?std::fmod(dx,dy):base=="pow"?std::pow(dx,dy):base=="acos"?std::acos(dx):base=="asin"?std::asin(dx):base=="atan"?std::atan(dx):base=="ceil"?std::ceil(dx):base=="cos"?std::cos(dx):base=="exp"?std::exp(dx):base=="floor"?std::floor(dx):base=="log"?std::log(dx):base=="log10"?std::log10(dx):base=="sin"?std::sin(dx):base=="tan"?std::tan(dx):std::sqrt(dx);
                uint64_t bits=0;std::memcpy(&bits,&r,8);
                cpu->Regs()[0]=uint32_t(bits);cpu->Regs()[1]=uint32_t(bits>>32);
                goto done;
            }
        }
        else if(name.rfind("gl",0)==0)result=gl_dispatch(name,a,b,c,d);
        else {
            trace.push_back("UNSUP-IMPORT "+name+" a=0x"+to_hex(a)+" b=0x"+to_hex(b)+" c=0x"+to_hex(c)+" d=0x"+to_hex(d)+" pc=0x"+to_hex(cpu->Regs()[15]-8)+" lr=0x"+to_hex(cpu->Regs()[14]-8)+" sp=0x"+to_hex(cpu->Regs()[13])+" got088="+to_hex(MemoryRead32(0x1043f088))+" canary=0x"+to_hex(MemoryRead32(0x104a1000))+" sym30007540=0x"+to_hex(MemoryRead32(0x30007540)));
            throw std::runtime_error("Unsupported import: "+name);
        }
        cpu->Regs()[0]=result;
        goto done;
    done:
        const auto lr=cpu->Regs()[14];cpu->Regs()[15]=lr&~1u;
        cpu->SetCpsr((cpu->Cpsr()&~0x20u)|((lr&1)?0x20u:0));
    }

    uint32_t gl_dispatch(const std::string& name,uint32_t a,uint32_t b,uint32_t,uint32_t) {
        ++gl_calls;
        if(native_gl)return native_gl->call(name);
        if(name=="glGetIntegerv"){
            if(a==0x0BA2){MemoryWrite32(b,0);MemoryWrite32(b+4,0);MemoryWrite32(b+8,480);MemoryWrite32(b+12,640);}
            else if(a==0x0D33||a==0x0D34){MemoryWrite32(b,0);MemoryWrite32(b+4,0);MemoryWrite32(b+8,480);MemoryWrite32(b+12,640);}
            else if(a==0x0BA0){MemoryWrite32(b,2048);}
            else if(a==0x0C23||a==0x0BDF||a==0x0BDE||a==0x0BDD||a==0x0BDA){MemoryWrite32(b,1);}
            else if(a==0x0B21){MemoryWrite32(b,0xFFFFFFFF);}
            else MemoryWrite32(b,0);
            return 0;
        }
        if(name=="glGetString"){
            if(a==0x1F03){
                if(!ext_string)ext_string=guest_string("GL_OES_compressed_paletted_texture GL_OES_framebuffer_object");
                return ext_string;
            }
            return 0;
        }
        if(name=="glGetError")return 0;
        if(name=="glGetBooleanv"||name=="glGetFloatv"||name=="glGetFixedv"){MemoryWrite32(b,0);return 0;}
        return 0;
    }

public:
    ds::GuestImage image;
    Dynarmic::A32::Jit* cpu=nullptr;
    FastTable* fast_page_table(){return fast_pages.get();}

    uint8_t MemoryRead8(uint32_t a) override {
        const auto hook=extra_code.find(a&~3u);if(hook!=extra_code.end())return uint8_t(hook->second>>(8*(a&3)));
        if(a==0&&++guard_log<=20)trace.push_back("  RD0 pc=0x"+to_hex(cpu->Regs()[15]-8)+" lr=0x"+to_hex(cpu->Regs()[14]-8)+" sp=0x"+to_hex(cpu->Regs()[13])+" r0=0x"+to_hex(cpu->Regs()[0])+" r1=0x"+to_hex(cpu->Regs()[1])+" r2=0x"+to_hex(cpu->Regs()[2])+" r3=0x"+to_hex(cpu->Regs()[3])+" r4=0x"+to_hex(cpu->Regs()[4])+" r5=0x"+to_hex(cpu->Regs()[5])+" r6=0x"+to_hex(cpu->Regs()[6])+" r7=0x"+to_hex(cpu->Regs()[7]));
        if(a==0x1044bb6c&&++guard_log<=40)trace.push_back("  RD 0x1044bb6c pc=0x"+to_hex(cpu->Regs()[15]-8)+" lr=0x"+to_hex(cpu->Regs()[14]-8)+" r4=0x"+to_hex(cpu->Regs()[4])+" r5=0x"+to_hex(cpu->Regs()[5])+" r6=0x"+to_hex(cpu->Regs()[6]));
        if((a==0x10450b68u||a==0x10450b69u||a==0x10450b6au||a==0x10450b6bu)&&++guard_log<=40)trace.push_back("  RD 0x10450b68 pc=0x"+to_hex(cpu->Regs()[15]-8)+" r4=0x"+to_hex(cpu->Regs()[4])+" r5=0x"+to_hex(cpu->Regs()[5])+" r6=0x"+to_hex(cpu->Regs()[6])+" r7=0x"+to_hex(cpu->Regs()[7]));
        if(mapped(a)){const auto it=pages.find(a>>12);if(it!=pages.end())return it->second[a&4095];uint8_t rv=0;if(!image.read(a,&rv,1)){if(!(a>=arena&&a<arena_end)&&!(a>=heap&&a<heap+0x4000000))fail("Unmapped guest read at "+std::to_string(a));return rv;}return rv;}
        if(a>=thunks && uint64_t(a)-thunks<functions.size()*4) return uint8_t((0xef000001u+((a-thunks)/4))>>(8*((a-thunks)%4)));
        if(a>=stop && a-stop<4)return uint8_t(0xef00ffffu>>(8*(a-stop)));
        uint8_t v=0;if(!image.read(a,&v,1)){uint32_t sp=cpu->Regs()[13];std::string st="  STACK@0x"+to_hex(sp-0x20)+":";for(int i=0;i<24;i++){uint32_t w=0;if(mapped(sp-0x20+i*4)&&image.read(sp-0x20+i*4,&w,4))st+=" "+to_hex(w);else st+=" ?";}trace.push_back(st);fail("Unmapped guest read at "+std::to_string(a)+" img="+std::to_string(image.memory.size()));}return v;
    }
    uint16_t MemoryRead16(uint32_t a) override {return MemoryRead8(a)|uint16_t(MemoryRead8(a+1))<<8;}
    uint32_t MemoryRead32(uint32_t a) override {
        const uint32_t v=MemoryRead16(a)|uint32_t(MemoryRead16(a+2))<<16;
        if(a==0x10444100u&&trace.size()<6000)trace.push_back("  RD32 0x10444100="+to_hex(v)+" @"+to_hex(cpu->Regs()[15])+" r3=0x"+to_hex(cpu->Regs()[3])+" r4=0x"+to_hex(cpu->Regs()[4]));
        if(cpu&&v==0x1031e5a4u&&trace.size()<6000)trace.push_back("  GOT 0x1031e5a4 read @"+to_hex(cpu->Regs()[15]));
        return v;
    }
    uint64_t MemoryRead64(uint32_t a) override {return MemoryRead32(a)|uint64_t(MemoryRead32(a+4))<<32;}
    void MemoryReadBlock(uint32_t address,void* output,size_t length) override {
        if(length>64*1024*1024||uint64_t(address)+length>0x100000000ull)throw std::runtime_error("Bulk guest read bounds");
        auto* bytes=static_cast<uint8_t*>(output);
        while(length){
            const auto count=std::min<size_t>(length,4096-(address&4095));
            MemoryRead8(address);MemoryRead8(address+uint32_t(count)-1);
            if(!fault.empty())throw std::runtime_error(fault);
            const auto it=pages.find(address>>12);
            if(it!=pages.end())std::memcpy(bytes,it->second.data()+(address&4095),count);
            else if(!image.read(address,bytes,count))for(size_t i=0;i<count;++i)bytes[i]=MemoryRead8(address+uint32_t(i));
            address+=uint32_t(count);bytes+=count;length-=count;
        }
    }
    void MemoryWriteBlock(uint32_t address,const void* input,size_t length) override {
        if(length>64*1024*1024||uint64_t(address)+length>0x100000000ull)throw std::runtime_error("Bulk guest write bounds");
        const auto* bytes=static_cast<const uint8_t*>(input);
        while(length){
            const auto count=std::min<size_t>(length,4096-(address&4095));
            MemoryWrite8(address,bytes[0]);MemoryWrite8(address+uint32_t(count)-1,bytes[count-1]);
            if(!fault.empty())throw std::runtime_error(fault);
            const auto it=pages.find(address>>12);
            if(it!=pages.end())std::memcpy(it->second.data()+(address&4095),bytes,count);
            else for(size_t i=0;i<count;++i)MemoryWrite8(address+uint32_t(i),bytes[i]);
            address+=uint32_t(count);bytes+=count;length-=count;
        }
    }
    std::optional<uint32_t> MemoryReadCode(uint32_t a) override {
        if((a&~1u)==0x10235944u&&++guard_log<=40)trace.push_back("  CODE 0x235944 r0=0x"+to_hex(cpu->Regs()[0])+" r1=0x"+to_hex(cpu->Regs()[1])+" r2=0x"+to_hex(cpu->Regs()[2])+" r3=0x"+to_hex(cpu->Regs()[3])+" r4=0x"+to_hex(cpu->Regs()[4])+" r5=0x"+to_hex(cpu->Regs()[5])+" r6=0x"+to_hex(cpu->Regs()[6])+" r7=0x"+to_hex(cpu->Regs()[7])+" r8=0x"+to_hex(cpu->Regs()[8])+" lr=0x"+to_hex(cpu->Regs()[14])+" lit=0x"+to_hex(MemoryRead32(0x1023599c)));
        if((a&~1u)==0x102f7efcu&&call277efc++<20)trace.push_back("  CALL 0x2f7efc r0=0x"+to_hex(cpu->Regs()[0])+" lr=0x"+to_hex(cpu->Regs()[14]-8)+" sp=0x"+to_hex(cpu->Regs()[13])+" r2=0x"+to_hex(cpu->Regs()[2])+" r3=0x"+to_hex(cpu->Regs()[3])+" r5=0x"+to_hex(cpu->Regs()[5])+" r6=0x"+to_hex(cpu->Regs()[6])+" r9=0x"+to_hex(cpu->Regs()[9]));
        if((a&~1u)==0x102f8778u&&call28778++<30)trace.push_back("  CALL 0x2f8778 r0=0x"+to_hex(cpu->Regs()[0])+" r1=0x"+to_hex(cpu->Regs()[1])+" lr=0x"+to_hex(cpu->Regs()[14]-8)+" sp=0x"+to_hex(cpu->Regs()[13])+" r9=0x"+to_hex(cpu->Regs()[9]));
        if(a!=stop && !extra_code.count(a) && !(a>=thunks && uint64_t(a)-thunks+4<=functions.size()*4) && !image.executable(a,4)){fail("Unmapped guest execution");return std::nullopt;}
        return MemoryRead32(a);
    }
    void MemoryWrite8(uint32_t a,uint8_t v) override {
        if(a==0x1043f088u&&++svc_log<=20)trace.push_back("  WR088 v="+to_hex(v)+" mapped="+std::to_string(mapped(a))+" pcount="+std::to_string(pages.count(a>>12)));
        if((a>=0x1044bb58u&&a<0x1044bb90u||a>=0x10450b60u&&a<0x10450b90u)&&++guard_log<=500)trace.push_back("  WR 0x"+to_hex(a)+"=0x"+to_hex(v)+" svc="+last_service+" pc=0x"+to_hex(cpu->Regs()[15])+" sp=0x"+to_hex(cpu->Regs()[13])+" r6=0x"+to_hex(cpu->Regs()[6])+" r7=0x"+to_hex(cpu->Regs()[7])+" r3=0x"+to_hex(cpu->Regs()[3])+" r2=0x"+to_hex(cpu->Regs()[2]));
        if(a==0x1043f100u&&++svc_log<=80)trace.push_back("  WRF100 0x"+to_hex(v)+" svc="+last_service+" pc=0x"+to_hex(cpu->Regs()[15]-8)+" r0=0x"+to_hex(cpu->Regs()[0])+" r1=0x"+to_hex(cpu->Regs()[1])+" r2=0x"+to_hex(cpu->Regs()[2])+" r3=0x"+to_hex(cpu->Regs()[3])+" r4=0x"+to_hex(cpu->Regs()[4])+" r5=0x"+to_hex(cpu->Regs()[5])+" r6=0x"+to_hex(cpu->Regs()[6]));
        if(mapped(a)){
            if(a==0x1043f088u)trace.push_back("  WR88-PATH mapped-branch page=0x"+to_hex(a>>12)+" memsize=0x"+to_hex(image.memory.size()));
            const uint32_t page=a>>12;
            if(!pages.count(page)){
                auto& arr=pages[page];
                const uint32_t imbase=0x10000000u,gp=page<<12;
                if(gp>=imbase && uint64_t(gp)-imbase<image.memory.size()){
                    const uint32_t off=gp-imbase,n=std::min<uint32_t>(4096,uint32_t(image.memory.size()-off));
                    std::memcpy(arr.data(),image.memory.data()+off,n);
                }
            }
            auto& entry=pages.at(a>>12);entry[a&4095]=v;
            if(fast_writable(a&~4095u))(*fast_pages)[a>>12]=entry.data();
            return;
        }
        if(!image.write(a,&v,1))fail("Write outside writable guest mapping at "+std::to_string(a));
    }
    void MemoryWrite16(uint32_t a,uint16_t v) override {MemoryWrite8(a,v);MemoryWrite8(a+1,v>>8);}
    void MemoryWrite32(uint32_t a,uint32_t v) override {MemoryWrite16(a,v);MemoryWrite16(a+2,v>>16);}
    void MemoryWrite64(uint32_t a,uint64_t v) override {MemoryWrite32(a,v);MemoryWrite32(a+4,v>>32);}
    void InterpreterFallback(uint32_t,size_t) override {fail("Unavailable instruction interpreter");}
    void ExceptionRaised(uint32_t,Dynarmic::A32::Exception) override {fail("Guest CPU exception");}
    void CallSVC(uint32_t id) override {if(id==65535)returned=true;else pending=id;cpu->HaltExecution();}
    void AddTicks(uint64_t n) override {budget=n>budget?0:budget-n;quantum=n>quantum?0:quantum-n;if(!graphics.realtime_clock)threads.tick(std::min<uint64_t>(n,1000000)*10);}
    uint64_t GetTicksRemaining() override {return std::min(budget,quantum);}

    uint32_t bind(const ds::GuestImage::Import& item) {
        if(item.type!=1){functions.push_back(item.name);return thunks+4*(functions.size()-1);}
        const auto p=alloc(std::max<uint32_t>(item.size,4096));
        if(item.name=="__stack_chk_guard")MemoryWrite32(p,0xa5a55a5a);
        else if(item.name=="__page_size")MemoryWrite32(p,4096);
        else if(item.name=="_tolower_tab_"||item.name=="_toupper_tab_"){
            const auto table=alloc(514);MemoryWrite16(table,0xffff);
            for(unsigned cc=0;cc<256;++cc){const auto v=item.name=="_tolower_tab_"&&cc>=65&&cc<=90?cc+32:item.name=="_toupper_tab_"&&cc>=97&&cc<=122?cc-32:cc;MemoryWrite16(table+2*(cc+1),v);}
            MemoryWrite32(p,table);
        } else if(item.name=="_ctype_"){
            const auto table=alloc(257);
            for(unsigned cc=0;cc<256;++cc){unsigned f=(cc>=65&&cc<=90?1:0)|(cc>=97&&cc<=122?2:0)|(cc>=48&&cc<=57?4:0);if(cc==32||(cc>=9&&cc<=13))f|=8;if(cc<32||cc==127)f|=32;if((cc>=48&&cc<=57)||(cc>=65&&cc<=70)||(cc>=97&&cc<=102))f|=64;if(cc==32)f|=128;if(cc>=33&&cc<=126&&!(f&7))f|=16;MemoryWrite8(table+cc+1,f);}
            MemoryWrite32(p,table);
        } else if(item.name!="__sF"&&item.name!="__dso_handle"&&item.name!="tzname")throw std::runtime_error("Unknown imported object: "+item.name);
        return p;
    }

    void initialize(const std::string& data,ds::GraphicsBackend backend) {
        data_root=data;
#ifdef __SWITCH__
        storage=std::make_unique<ds::GuestStorage>(data_root,"sdmc:/switch/deadspacemobile/savedata");
#else
        storage=std::make_unique<ds::GuestStorage>(data_root,data_root+"/savedata");
#endif
        graphics=std::move(backend);
        if(graphics.resolve)native_gl=std::make_unique<ds::GuestGl>(graphics,ds::GlGuestMemory{
            [&](unsigned n){return arg(n);},[&](uint32_t n){return alloc(n);},
            [&](uint32_t p,void* out,size_t n){
                if(uint64_t(p)+n>0x100000000ull)throw std::runtime_error("GL guest read wraps");
                MemoryReadBlock(p,out,n);
                if(!fault.empty())throw std::runtime_error(fault);
            },
            [&](uint32_t p,const void* input,size_t n){
                if(uint64_t(p)+n>0x100000000ull)throw std::runtime_error("GL guest write wraps");
                MemoryWriteBlock(p,input,n);
                if(!fault.empty())throw std::runtime_error(fault);
            }});
        errno_address=alloc(4);threads.thread().error=errno_address;
        std::string error;if(!image.bind_imports([&](const auto& i){return bind(i);},error))throw std::runtime_error(error);
        // The upstream extracted-data VFS fix, translated into guest calls.
        // No completion flags are altered; both mounts traverse actual files.
        constexpr uint32_t call_offset=0x1c3d7c,expected=0xeb05265c,hook=0x10600000;
        uint32_t original=0;std::memcpy(&original,image.memory.data()+call_offset,4);
        if(original!=expected)throw std::runtime_error("Content mount call does not match the pinned donor");
        functions.push_back("DS:mount_content_root");extra_code[hook]=0xef000000|uint32_t(functions.size());extra_code[hook+4]=0xe12fff1e;
        const uint32_t branch=0xeb000000|uint32_t((int64_t(hook)-int64_t(ds::GuestImage::base+call_offset+8))/4)&0x00ffffff;
        std::memcpy(image.memory.data()+call_offset,&branch,4);
        if(!fault.empty())throw std::runtime_error(fault);
        // JNI/VM thunks append AFTER the game import thunks so SVC ids never collide.
        const uint32_t jni_base=uint32_t(functions.size());
        functions.resize(jni_base+jni_slots+vm_slots,"JNI:unsupported");
        const auto jslot=[&](unsigned idx,const char* n){functions[jni_base+idx]="JNI:"+std::string(n);};
        const auto vslot=[&](unsigned idx,const char* n){functions[jni_base+jni_slots+idx]="VM:"+std::string(n);};
        jslot(JNI_GetVersion,"GetVersion");jslot(JNI_FindClass,"FindClass");
        jslot(JNI_GetMethodID,"GetMethodID");jslot(JNI_GetStaticMethodID,"GetStaticMethodID");
        jslot(JNI_NewGlobalRef,"NewGlobalRef");jslot(JNI_DeleteGlobalRef,"DeleteGlobalRef");
        jslot(JNI_DeleteLocalRef,"DeleteLocalRef");jslot(JNI_NewLocalRef,"NewLocalRef");
        jslot(JNI_GetObjectClass,"GetObjectClass");jslot(JNI_IsSameObject,"IsSameObject");
        jslot(JNI_GetJavaVM,"GetJavaVM");jslot(JNI_ExceptionClear,"ExceptionClear");
        jslot(JNI_ExceptionCheck,"ExceptionCheck");jslot(JNI_ExceptionOccurred,"ExceptionOccurred");
        jslot(JNI_PushLocalFrame,"PushLocalFrame");jslot(JNI_PopLocalFrame,"PopLocalFrame");
        jslot(JNI_EnsureLocalCapacity,"EnsureLocalCapacity");jslot(JNI_MonitorEnter,"MonitorEnter");
        jslot(JNI_MonitorExit,"MonitorExit");jslot(JNI_NewObject,"NewObject");
        jslot(JNI_NewObjectV,"NewObjectV");jslot(JNI_NewObjectA,"NewObjectA");
        jslot(JNI_NewStringUTF,"NewStringUTF");jslot(JNI_NewString,"NewString");
        jslot(JNI_GetStringLength,"GetStringLength");jslot(JNI_GetStringUTFLength,"GetStringUTFLength");
        jslot(JNI_GetStringUTFChars,"GetStringUTFChars");jslot(JNI_GetStringChars,"GetStringChars");
        jslot(JNI_ReleaseStringUTFChars,"ReleaseStringUTFChars");jslot(JNI_ReleaseStringChars,"ReleaseStringChars");
        jslot(JNI_GetArrayLength,"GetArrayLength");jslot(JNI_GetObjectArrayElement,"GetObjectArrayElement");
        jslot(JNI_NewObjectArray,"NewObjectArray");
        jslot(JNI_NewBooleanArray,"NewBooleanArray");jslot(JNI_NewByteArray,"NewByteArray");
        jslot(JNI_NewCharArray,"NewCharArray");jslot(JNI_NewShortArray,"NewShortArray");
        jslot(JNI_NewIntArray,"NewIntArray");jslot(JNI_NewLongArray,"NewLongArray");
        jslot(JNI_NewFloatArray,"NewFloatArray");jslot(JNI_NewDoubleArray,"NewDoubleArray");
        ds::register_jni_variants(jslot);
        jslot(JNI_SetObjectArrayElement,"SetObjectArrayElement");
        jslot(JNI_Throw,"Throw");jslot(JNI_ThrowNew,"ThrowNew");
        vslot(3,"DestroyJavaVM");vslot(4,"AttachCurrentThread");vslot(5,"DetachCurrentThread");
        vslot(6,"GetEnv");vslot(7,"AttachCurrentThreadAsDaemon");
        jni_table=alloc(jni_slots*4);
        for(unsigned i=0;i<jni_slots;++i)MemoryWrite32(jni_table+i*4,thunks+(jni_base+i)*4);
        vm_table=alloc(vm_slots*4);
        for(unsigned i=0;i<vm_slots;++i)MemoryWrite32(vm_table+i*4,thunks+(jni_base+jni_slots+i)*4);
        env_addr=alloc(4);MemoryWrite32(env_addr,jni_table);
        vm_addr=alloc(4);MemoryWrite32(vm_addr,vm_table);
    }

    void install_stack_chk_guard() {
        // GOT 槽 0x1043f088 由 bind_imports 指向符号 __stack_chk_guard（0x30007540），
        // guest bionic 已写入默认金丝雀 0xa5a55a5a。此函数仅兜底：符号位置为空时补默认值。
        // 注意：不得把金丝雀放到 0x104a1000 附近——那是 guest 调用桥栈活动区（sp 实测 0x104a1010），会被覆盖。
        if(MemoryRead32(0x30007540)==0){MemoryWrite32(0x30007540,0xa5a55a5a);}
    }

    void run_constructors() {
        trace.push_back("pre-ctor GOT 0x1043f100="+to_hex(MemoryRead32(0x1043f100))+" raw="+to_hex(image.memory[0x43f100]|image.memory[0x43f101]<<8|image.memory[0x43f102]<<16|image.memory[0x43f103]<<24));
        for(const auto fn:image.constructors){
            if(!image.executable(fn&~1u,4))throw std::runtime_error("Invalid constructor address");
            guest_call(fn,{},"constructor",true);
            ++constructors_completed;
        }
    }

    void run_lifecycle(unsigned frames) {
        trace.push_back("Constructors returned: "+std::to_string(constructors_completed));
        if(graphics.record)graphics.record("Game constructors completed");
        trace.push_back("lit 0x23599c="+to_hex(MemoryRead32(0x1023599c))+" 0x2359a0="+to_hex(MemoryRead32(0x102359a0))+" 0x2359a8="+to_hex(MemoryRead32(0x102359a8))+" 0x2359a4="+to_hex(MemoryRead32(0x102359a4)));
        trace.push_back("GOT 0x1043f100="+to_hex(MemoryRead32(0x1043f100))+" 0x1043f0c0="+to_hex(MemoryRead32(0x1043f0c0))+" 0x1043ef90="+to_hex(MemoryRead32(0x1043ef90))+" 0x1043e8d0="+to_hex(MemoryRead32(0x1043e8d0))+" raw43f100="+to_hex(image.memory[0x43f100]|image.memory[0x43f101]<<8|image.memory[0x43f102]<<16|image.memory[0x43f103]<<24)+" raw43f0c0="+to_hex(image.memory[0x43f0c0]|image.memory[0x43f0c1]<<8|image.memory[0x43f0c2]<<16|image.memory[0x43f0c3]<<24));
        trace.push_back("post-ctor 0x1044bb58..0x1044bb8c: "+to_hex(MemoryRead32(0x1044bb58))+" "+to_hex(MemoryRead32(0x1044bb5c))+" "+to_hex(MemoryRead32(0x1044bb60))+" "+to_hex(MemoryRead32(0x1044bb64))+" "+to_hex(MemoryRead32(0x1044bb68))+" "+to_hex(MemoryRead32(0x1044bb6c))+" "+to_hex(MemoryRead32(0x1044bb70))+" "+to_hex(MemoryRead32(0x1044bb74))+" "+to_hex(MemoryRead32(0x1044bb78))+" "+to_hex(MemoryRead32(0x1044bb7c))+" "+to_hex(MemoryRead32(0x1044bb80))+" "+to_hex(MemoryRead32(0x1044bb84))+" "+to_hex(MemoryRead32(0x1044bb88))+" "+to_hex(MemoryRead32(0x1044bb8c)));
        trace.push_back("post-ctor 0x10455b40..0x10455b70: "+to_hex(MemoryRead32(0x10455b40))+" "+to_hex(MemoryRead32(0x10455b44))+" "+to_hex(MemoryRead32(0x10455b48))+" "+to_hex(MemoryRead32(0x10455b4c))+" "+to_hex(MemoryRead32(0x10455b50))+" "+to_hex(MemoryRead32(0x10455b54))+" "+to_hex(MemoryRead32(0x10455b58))+" "+to_hex(MemoryRead32(0x10455b5c))+" "+to_hex(MemoryRead32(0x10455b60))+" "+to_hex(MemoryRead32(0x10455b64))+" "+to_hex(MemoryRead32(0x10455b68))+" "+to_hex(MemoryRead32(0x10455b6c)));
        const auto onload=image.exports.find("JNI_OnLoad");
        if(onload==image.exports.end())throw std::runtime_error("JNI_OnLoad export missing");
        const uint32_t version=guest_call(onload->second,{vm_addr,0},"JNI_OnLoad",true);
        trace.push_back("JNI_OnLoad returned: 0x"+to_hex(version));
        if(graphics.record)graphics.record("JNI initialized");
        if(version!=0x10004)throw std::runtime_error("Unexpected JNI version");
        const auto audio=image.exports.find("Java_com_ea_EAAudioCore_AndroidEAAudioCore_Init");
        if(audio==image.exports.end())throw std::runtime_error("Audio init export missing");
        const uint32_t track=alloc(4);objects[track].cls="android/media/AudioTrack";
        guest_call(audio->second,{env_addr,0x42424242,track,1024*1024,2,44100},"EAAudioCore_Init",true);
        trace.push_back("EAAudioCore_Init returned");
        const auto create=image.exports.find("Java_com_ea_blast_MainActivity_NativeOnCreate");
        if(create==image.exports.end())throw std::runtime_error("NativeOnCreate export missing");
        guest_call(create->second,{},"NativeOnCreate",true);
        trace.push_back("NativeOnCreate returned");
        if(graphics.record)graphics.record("Game activity created");
        const auto surface=image.exports.find("Java_com_ea_blast_AndroidRenderer_NativeOnSurfaceCreated");
        if(surface==image.exports.end())throw std::runtime_error("NativeOnSurfaceCreated export missing");
        guest_call(surface->second,{},"NativeOnSurfaceCreated",true);
        trace.push_back("NativeOnSurfaceCreated returned");
        if(graphics.record)graphics.record("Game surface created");
        const auto visibility=image.exports.find("Java_com_ea_blast_KeyboardAndroid_NativeOnVisibilityChanged");
        if(visibility!=image.exports.end()){
            guest_call(visibility->second,{env_addr,0x42424242,600,1},"NativeOnVisibilityChanged",true);
            trace.push_back("NativeOnVisibilityChanged returned");
        }
        const auto frame=image.exports.find("Java_com_ea_blast_AndroidRenderer_NativeOnDrawFrame");
        if(frame==image.exports.end())throw std::runtime_error("NativeOnDrawFrame export missing");
        for(unsigned i=0;i<frames;++i){
            bool quit=false;
            if(graphics.poll_input)for(const auto& event:graphics.poll_input()){
                if(event.kind==ds::GuestInput::Kind::quit){quit=true;break;}
                const auto bits=[](float value){uint32_t result;std::memcpy(&result,&value,4);return result;};
                std::string name;
                std::vector<uint32_t> args={env_addr,0x42424242};
                if(event.kind==ds::GuestInput::Kind::key){name=event.down?"Java_com_ea_blast_KeyboardAndroid_NativeOnKeyDown":"Java_com_ea_blast_KeyboardAndroid_NativeOnKeyUp";args.insert(args.end(),{600,event.code,1});}
                else if(event.kind==ds::GuestInput::Kind::pointer){name="Java_com_ea_blast_TouchSurfaceAndroid_NativeOnPointerEvent";args.insert(args.end(),{event.code,event.module,event.id,bits(event.x),bits(event.y)});}
                else{name="Java_com_ea_blast_AccelerometerAndroidDelegate_NativeOnAcceleration";args.insert(args.end(),{bits(event.x),bits(event.y),bits(event.z)});}
                const auto entry=image.exports.find(name);if(entry==image.exports.end())throw std::runtime_error("Input export missing: "+name);
                guest_call(entry->second,args,"Input",true);
            }
            if(quit)break;
            if(graphics.record&&(i<5||i%120==0))graphics.record("BEGIN: game frame "+std::to_string(i+1));
            guest_call(frame->second,{},"NativeOnDrawFrame["+std::to_string(i)+"]",true);
            ++frames_done;
            if(graphics.record&&(frames_done<=5||frames_done%120==0))graphics.record("Game frame returned: "+std::to_string(frames_done));
            if(graphics.record&&(frames_done<=5||frames_done%120==0))graphics.record(graphics_report());
            if(graphics.present)graphics.present();
            if(i<5||i%3600==0)trace.push_back("NativeOnDrawFrame["+std::to_string(i)+"] returned");
        }
        for(const char* symbol:{"Java_com_ea_blast_MainActivity_NativeOnPause","Java_com_ea_blast_MainActivity_NativeOnStop"}){
            const auto entry=image.exports.find(symbol);
            if(entry!=image.exports.end()){guest_call(entry->second,{env_addr,0x42424242},symbol,true);if(graphics.record)graphics.record(std::string(symbol)+" returned");}
        }
    }

    static std::string to_hex(uint32_t v){char buf[16];std::snprintf(buf,sizeof(buf),"%08x",v);return buf;}
    std::string graphics_report() const {
        if(!native_gl)return "Graphics backend: mock diagnostics.\n";
        std::string out=std::string("Graphics backend: ")+(graphics.desktop_compatibility?"desktop compatibility reference":"native GLES1 subset")+"; "+std::to_string(native_gl->calls)+" guest GL dispatches, "+std::to_string(native_gl->draws)+" draw submissions, "+std::to_string(native_gl->texture_uploads)+" texture uploads, "+std::to_string(native_gl->atc_uploads)+" ATC uploads.\n";
        if(graphics.hd_texture)out+="HD: "+std::to_string(native_gl->hd_matches)+" matches, "+std::to_string(native_gl->hd_mip_uploads)+" mip uploads, "+std::to_string(native_gl->hd_misses)+" missing, "+std::to_string(native_gl->hd_rejected)+" rejected, "+std::to_string(native_gl->hd_budget_skips)+" budget skips, "+std::to_string(native_gl->hd_dynamic_fallbacks)+" dynamic fallbacks.\n";
        if(!native_gl->last_hd_error.empty())out+="Last HD rejection: "+native_gl->last_hd_error+"\n";
        return out;
    }

    std::string report() const {
        std::string out="PASS: "+std::to_string(constructors_completed)+" constructors; JNI_OnLoad+audio+create+surface+visibility OK; "+std::to_string(frames_done)+" DrawFrame returned; "+std::to_string(jni_calls)+" JNI/Java calls; "+std::to_string(gl_calls)+" GL calls.\n";
        out+=native_gl?"Native GL bridge active; lifecycle returns alone do not verify game rendering, audio or gameplay.\n":"Diagnostic result only: GL calls are mocked; no game rendering, audible audio or gameplay verified.\n";
        out+="Threads: cooperative scheduler; created="+std::to_string(threads_created)+" context switches="+std::to_string(thread_switches)+" (not native parallel execution).\n";
        out+="Audio: "+std::to_string(audio_samples)+" PCM samples accepted by backend; listening verification is separate.\n";
        out+=graphics_report();
        out+="Assets: "+std::to_string(asset_opens)+" streams opened; "+std::to_string(asset_lists)+" directories listed; "+std::to_string(asset_read_bytes)+" bytes delivered to guest.\n";
        for(const auto& path:asset_paths)if(path.find("stringdata/")!=std::string::npos)out+="Language resource opened: "+path+"\n";
        for(const auto& kv:service_counts)if(kv.second)out+="  svc "+kv.first+" "+std::to_string(kv.second)+"\n";
        for(const auto& t:trace)out+=t+"\n";
        return out;
    }
    std::string trace_report() const {
        std::string out=graphics_report();
        out+="Assets: "+std::to_string(asset_opens)+" streams opened; "+std::to_string(asset_read_bytes)+" bytes delivered to guest.\n";
        for(const auto& t:trace)out+=t+"\n";
        return out;
    }
    unsigned done() const {return constructors_completed;}
};
}

std::string lifecycle_probe(const std::string& library,const std::string& data_root,unsigned frames,ds::GraphicsBackend graphics) {
    const auto record=graphics.record;
    if(record)record("BEGIN: guest runtime allocation");
    LifecycleRuntime env;
    try {
        if(record)record("END: guest runtime allocation; BEGIN: load original ELF");
        std::string error;if(!env.image.load(library,error))throw std::runtime_error(error);
        //env.trace.push_back("image memory bytes="+std::to_string(env.image.memory.size()));
        if(record)record("END: load original ELF; BEGIN: runtime initialization");
        env.initialize(data_root,std::move(graphics));
        if(record)record("END: runtime initialization; BEGIN: JIT construction");
        Dynarmic::A32::UserConfig conf;conf.callbacks=&env;conf.arch_version=Dynarmic::A32::ArchVersion::v6K;
        conf.code_cache_size=32*1024*1024;conf.page_table=env.fast_page_table();conf.fastmem_pointer=std::nullopt;
        conf.detect_misaligned_access_via_page_table=8|16|32|64;
        conf.only_detect_misalignment_via_page_table_on_page_boundary=true;
        Dynarmic::A32::Jit cpu(conf);env.cpu=&cpu;
        if(record)record("END: JIT construction; BEGIN: guest constructors");
        env.install_stack_chk_guard();
        env.run_constructors();
        env.run_lifecycle(frames);
        return env.report();
    } catch(const std::exception& e) {
        if(record)record(std::string("RUNTIME ERROR: ")+e.what());
        std::string out="BLOCKED after "+std::to_string(env.done())+" constructors: "+e.what()+"\n";
        out+=env.trace_report();
        return out;
    }
}
