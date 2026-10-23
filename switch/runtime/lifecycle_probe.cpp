// 0.3.1 lifecycle probe: bounded ARM32 JNI/JavaVM migration from
// switch/tools/boot_probe.py into the Switch C++ runtime.
// Guest addresses stay 32-bit integers; host services are dispatched by SVC id.
#include "lifecycle_probe.hpp"
#include "guest_image.hpp"
#include "guest_call_scope.hpp"
#include "guest_heap.hpp"
#include "guest_arena.hpp"
#include "guest_sort.hpp"
#include "guest_numeric.hpp"
#include "guest_files.hpp"
#include "guest_trace.hpp"
#include "guest_clock.hpp"
#include "guest_paths.hpp"
#include "guest_stat.hpp"
#include "guest_threads.hpp"
#include "guest_jump.hpp"
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
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
constexpr uint32_t heap=ds::GuestLayout::heap_base, arena=0x40000000, arena_end=0x50000000;
constexpr uint32_t stack=ds::GuestLayout::stack_base, thunks=0x60000000, stop=0x70000000;
constexpr unsigned jni_slots=233, vm_slots=8;

using ds::MethodKey;

class LifecycleRuntime final: public Dynarmic::A32::UserCallbacks, public ds::GuestJni {
    using Page=std::array<uint8_t,4096>;
    std::unordered_map<uint32_t,Page> pages;
    std::vector<std::string> functions;
    ds::GuestHeap guest_heap;
    using FastPages=std::array<uint8_t*,Dynarmic::A32::UserConfig::NUM_PAGE_TABLE_ENTRIES>;
    std::unique_ptr<FastPages> fast_pages=std::make_unique<FastPages>();
    uint64_t fast_page_count=0;
    // Only permanent, fully mapped heap/main-stack pages are eligible. Worker
    // stacks, mmap/sbrk and image permissions continue through checked callbacks.
    Page& sparse_page(uint32_t address) {
        auto& page=pages[address>>12];
        const uint64_t begin=uint64_t(address&~4095u),end=begin+4096;
        if(((begin>=heap&&end<=guest_heap.high_water())||
            (begin>=stack&&end<=uint64_t(stack)+0x200000))&&!(*fast_pages)[address>>12]) {
            (*fast_pages)[address>>12]=page.data();++fast_page_count;
        }
        return page;
    }
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
    std::set<uint32_t> save_descriptors,save_streams;
    uint64_t save_read_bytes=0,save_written_bytes=0,save_opens=0;
    ds::GuestTrace trace;
    std::string data_root;
    ds::GuestStoragePaths storage;
    ds::GuestArena arena_memory{arena,arena_end};
    uint32_t pending=0,errno_address=0,env_addr=0,vm_addr=0,jni_table=0,vm_table=0;
    uint32_t ext_string=0;
    bool returned=false;
    uint64_t budget=0;
    const uint64_t epoch_ns=uint64_t(std::time(nullptr))*1000000000ull;
    std::string fault;
    unsigned constructors_completed=0,service_calls=0,jni_calls=0,gl_calls=0,frames_done=0,strstr_log=0,asset_reads=0,guard_log=0,svc_log=0,call277efc=0,call28778=0;std::string last_service;
    std::map<std::string,uint64_t> service_counts;
    ds::GraphicsBackend graphics;
    ds::GuestClockFloor clock_floor;
    uint64_t perf_frames=0,input_ns=0,draw_ns=0,present_ns=0,max_frame_ns=0;
    static uint64_t elapsed_ns(std::chrono::steady_clock::time_point start) {
        return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());
    }
    uint64_t guest_time() {
        const auto previous=threads.time();
        if(graphics.monotonic_ns)threads.tick(clock_floor.sample(graphics.monotonic_ns(),previous)-previous);
        return threads.time();
    }
    std::unique_ptr<ds::GuestGl> native_gl;

    bool mapped(uint32_t a) const {
        return threads.mapped(a) || (a>=heap && a<guest_heap.high_water()) ||
               arena_memory.contains(a);
    }
    uint64_t direct_read_bytes=0,direct_write_bytes=0;
    bool read_span(uint32_t address,void* output,size_t count) {
        if(!count)return true;
        if(uint64_t(address)+count>0x100000000ull)return false;
        const uint32_t last=address+uint32_t(count-1);
        if((address>>12)==(last>>12)&&mapped(address)&&mapped(last)) {
            std::memcpy(output,sparse_page(address).data()+(address&4095),count);
        } else if(!image.read(address,output,count))return false;
        direct_read_bytes+=count;return true;
    }
    bool write_span(uint32_t address,const void* input,size_t count) {
        if(!count)return true;
        if(uint64_t(address)+count>0x100000000ull)return false;
        const uint32_t last=address+uint32_t(count-1);
        if((address>>12)==(last>>12)&&mapped(address)&&mapped(last))
            std::memcpy(sparse_page(address).data()+(address&4095),input,count);
        else if(!image.write(address,input,count))return false;
        direct_write_bytes+=count;return true;
    }
    void MemoryReadBytes(uint32_t address,void* output,size_t count) override {
        ds::read_guest_bytes(address,output,count,[&](uint32_t p,void* out,size_t n){return read_span(p,out,n);},[&](uint32_t p){return MemoryRead8(p);});
    }
    void MemoryWriteBytes(uint32_t address,const void* input,size_t count) override {
        ds::write_guest_bytes(address,input,count,[&](uint32_t p,const void* in,size_t n){return write_span(p,in,n);},[&](uint32_t p,uint8_t v){MemoryWrite8(p,v);});
    }
    void fill_bytes(uint32_t address,uint8_t value,uint32_t count) {
        ds::transfer_range(address,count);
        std::array<uint8_t,4096> bytes;bytes.fill(value);
        while(count){const auto n=std::min<uint32_t>(count,4096);MemoryWriteBytes(address,bytes.data(),n);address+=n;count-=n;}
    }
    void fail(const std::string& message) {
        if(fault.empty()){
            std::string ctx=message;
            if(cpu){ctx+=" pc="+to_hex(cpu->Regs()[15]);for(int i=0;i<13;++i)ctx+=" r"+std::to_string(i)+"="+to_hex(cpu->Regs()[i]);}
            fault=ctx;
        }
        if(cpu)cpu->HaltExecution();
        budget=0;
    }
    uint32_t alloc(uint32_t size) override {return guest_heap.allocate(size);}
    uint32_t arg(unsigned n) {return n<4?cpu->Regs()[n]:MemoryRead32(cpu->Regs()[13]+4*(n-4));}
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
    uint64_t java_delegate(uint32_t obj,const MethodKey& m,const std::vector<uint64_t>& values) {
        ++jni_calls;
        const std::string& cls=objects[obj].cls;
        trace.push_back("Java "+cls+"."+m.name+m.sig);
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
        if(const auto result=audio_delegate(obj,m,values))return *result;
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
              {"GetLanguage","zh"},{"GetLocale","zh_CN"},{"GetTotalRAM","-1"},{"GetTouchPadCount","1"},
              {"GetTouchScreenCount","1"},{"GetTrackBallCount","0"},{"GetVibratorCount","0"},
              {"GetVirtualKeyboardCount","1"}};
            const auto it=props.find(m.name);if(it!=props.end()){if((m.name=="GetLanguage"||m.name=="GetLocale")&&graphics.progress)graphics.progress("LOCALE: "+m.name+"="+it->second);return java_string(it->second);}
        }
        if(cls=="com/ea/blast/GetAppDataDirectoryDelegate"){
            if(m.name=="GetAppDataDirectory"||m.name=="GetExternalStorageDirectory")return java_string(storage.save_root.empty()?data_root:storage.save_root);
        }
        if(cls=="com/ea/blast/DisplayAndroidDelegate"){
            // Android reports natural portrait dimensions; the engine swaps them.
            if(m.name=="GetDefaultWidth")return graphics.height;
            if(m.name=="GetDefaultHeight")return graphics.width;
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
        const bool diagnostic=top_level&&label!="constructor"&&label.rfind("NativeOnDrawFrame",0)!=0;
        if(diagnostic&&graphics.progress)graphics.progress("BEGIN: "+label);
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
        budget=6000000000;returned=false;pending=0;
        unsigned calls=0;
        unsigned beats=0;
        auto last_checkpoint=std::chrono::steady_clock::now();
        // Import calls end a JIT run, not a scheduling timeslice. Keep the
        // remaining budget across them so import-heavy resource loaders can
        // make progress between display frames, while bounding CPU-only loops.
        quantum=std::min<uint64_t>(budget,1000000);
        while(!returned&&fault.empty()&&budget){
            if(!quantum)quantum=std::min<uint64_t>(budget,1000000);
            cpu->Run();
            const auto& active=threads.thread();
            const auto sp=cpu->Regs()[13];
            if(fault.empty() && (sp<active.stack_begin || uint64_t(sp)>uint64_t(active.stack_begin)+active.stack_size)) {
                throw std::runtime_error("Guest stack escaped thread mapping: sp="+to_hex(sp)+" pc="+to_hex(cpu->Regs()[15])+" lr="+to_hex(cpu->Regs()[14])+" last="+last_service);
            }
            if((++beats&1023)==0&&graphics.progress) {
                const auto now=std::chrono::steady_clock::now();
                if(now-last_checkpoint>=std::chrono::seconds(10)) {
                    const std::string current=pending&&pending<=functions.size()?functions[pending-1]:last_service;
                    graphics.progress("HEARTBEAT: "+label+" pc=0x"+to_hex(cpu->Regs()[15])+" service="+current+" instructions_left="+std::to_string(budget)+" asset_bytes="+std::to_string(asset_read_bytes)+" guest_gl="+std::to_string(gl_calls)+" direct_rw_bytes="+std::to_string(direct_read_bytes+direct_write_bytes));
                    last_checkpoint=now;
                }
            }
            if(pending){const auto id=pending;pending=0;if(id>functions.size()||++calls>50000000)throw std::runtime_error("Import dispatch limit "+label);++service_calls;if(cpu->Regs()[15]>=0x10235800u&&cpu->Regs()[15]<0x10235960u&&++svc_log<=40)trace.push_back("  SVC "+functions[id-1]+" guest_pc=0x"+to_hex(cpu->Regs()[15]-8)+" a=0x"+to_hex(cpu->Regs()[0])+" b=0x"+to_hex(cpu->Regs()[1])+" c=0x"+to_hex(cpu->Regs()[2])+" d=0x"+to_hex(cpu->Regs()[3]));service(functions[id-1]);}
            if(!outer)continue;
            if(!fault.empty())break;
            if(returned) {
                if(threads.current()==1)break;
                trace.push_back("guest thread exited id="+std::to_string(threads.current()));
 …16236 tokens truncated…const auto va=r.alloc(24);r.MemoryWrite32(va,array);r.MemoryWrite32(va+4,17);r.put64(va+8,0xfedcba9876543210ull);
        r.call("CallLongMethodV",{recv,method,va});
        check(r.values==std::vector<uint64_t>{array,17,0xfedcba9876543210ull},"va_list decoding failed");
        const auto align_method=r.method(recv,"echo","(IJ)J");
        r.MemoryWrite32(va,19);r.MemoryWrite32(va+4,0xcccccccc);r.put64(va+8,0x123456789abcdef0ull);
        r.call("CallLongMethodV",{recv,align_method,va});
        check(r.values==std::vector<uint64_t>{19,0x123456789abcdef0ull},"va_list did not align 64-bit value");
        const auto fmethod=r.method(recv,"echo","(F)J");
        r.call("CallLongMethod",{recv,fmethod,0xdeadbeef,0,0x3ff80000}); // promoted double 1.5, r3 skipped
        check(r.values==std::vector<uint64_t>{0x3fc00000},"Variadic float promotion failed");
        r.MemoryWrite32(argv,0x3fc00000);r.call("CallLongMethodA",{recv,fmethod,argv});
        check(r.values==std::vector<uint64_t>{0x3fc00000},"jvalue float incorrectly promoted");
        reject([&]{r.method(recv,"echo","([BII");});
        reject([&]{r.method(recv,"echo","([V)V");});
        char pattern[]="/tmp/deadspacemobile-jni-XXXXXX";
        char* dir=mkdtemp(pattern);check(dir!=nullptr,"mkdtemp failed");
        struct Cleanup {std::filesystem::path p;~Cleanup(){std::filesystem::remove_all(p);}} cleanup{dir};
        r.root=dir;std::filesystem::create_directories(r.root+"/assets/nested");
        std::ofstream(r.root+"/assets/b.bin",std::ios::binary)<<"abcdef";
        std::ofstream(r.root+"/assets/a.bin",std::ios::binary)<<"1234";
        const auto manager=r.instance("android/content/res/AssetManager");
        const auto open=r.method(manager,"open","(Ljava/lang/String;)Ljava/io/InputStream;");
        const auto stream1=uint32_t(r.call("CallObjectMethod",{manager,open,r.text("b.bin")}));
        const auto stream2=uint32_t(r.call("CallObjectMethod",{manager,open,r.text("a.bin")}));
        const auto read=r.method(stream1,"read","([BII)I");
        check(r.call("CallIntMethod",{stream1,read,array,1,3})==3,"Asset stream read failed");
        check(r.MemoryRead8(r.data(array)+1)=='a' && r.position(stream2)==0,"Stream receiver/position corrupted");
        const auto skip=r.method(stream1,"skip","(J)J");
        check(r.call("CallLongMethod",{stream1,skip,0xdeadbeef,0xffffffff,0xffffffff})==0 && r.position(stream1)==3,"Negative skip moved backwards");
        check(r.call("CallIntMethod",{stream1,read,array,0,8})==3,"Read did not clamp at EOF");
        check(r.call("CallIntMethod",{stream1,read,array,0,0})==0,"Zero read at EOF must return zero");
        check(uint32_t(r.call("CallIntMethod",{stream1,read,array,0,1}))==0xffffffff,"EOF must return -1");
        r.call("CallIntMethod",{stream2,read,array,7,2});
        check(r.exception_class()=="java/lang/IndexOutOfBoundsException" && r.position(stream2)==0,"Bad range truncated instead of raising exception");
        r.call("ExceptionClear");
        check(r.call("CallObjectMethod",{manager,open,r.text("missing.zip")})==0 && r.call("ExceptionCheck")==1,"Missing asset did not raise Java exception");
        check(r.exception_class()=="java/io/FileNotFoundException","Wrong missing asset exception");r.call("ExceptionClear");
        const auto list=r.method(manager,"list","(Ljava/lang/String;)[Ljava/lang/String;");
        const auto listing=uint32_t(r.call("CallObjectMethod",{manager,list,r.text("")}));
        check(r.call("GetArrayLength",{listing})==3,"Asset directory list is a stub");
        check(r.string(uint32_t(r.call("GetObjectArrayElement",{listing,0})))=="a.bin","Asset directory listing not sorted");
        r.call("CallObjectMethod",{manager,open,r.text("../b.bin")});
        check(r.exception_class()=="java/io/IOException","Asset traversal accepted");r.call("ExceptionClear");
        const auto close=r.method(stream2,"close","()V");r.call("CallVoidMethod",{stream2,close});
        r.call("CallIntMethod",{stream2,read,array,0,1});check(r.exception_class()=="java/io/IOException","Closed asset readable");
        TestRuntime sound;uint32_t queued=0;bool paused=true;unsigned opens=0;std::vector<uint8_t> captured;
        const auto track=sound.audio_init({[&](unsigned hz,unsigned channels){check(hz==44100&&channels==2,"PCM format");++opens;},[&](bool v){paused=v;},[&](const uint8_t* data,uint32_t n){captured.insert(captured.end(),data,data+n);queued+=n;},[&](){return queued;},[&](){queued=0;}});
        const auto play=sound.method(track,"play","()V"),stop=sound.method(track,"stop","()V");
        sound.call("CallVoidMethod",{track,play});check(!paused&&opens==1,"Audio play did not open output");
        const auto samples=uint32_t(sound.call("NewShortArray",{8})),pcm_source=sound.data(samples);
        sound.MemoryWrite16(pcm_source+4,0x8000);sound.MemoryWrite16(pcm_source+6,0x7fff);sound.MemoryWrite16(pcm_source+8,1);sound.MemoryWrite16(pcm_source+10,0xffff);
        const auto write=sound.method(track,"write","([SII)I");
        check(sound.call("CallIntMethod",{track,write,samples,2,4})==4,"PCM short count is not byte count");
        check(captured==std::vector<uint8_t>({0,128,255,127,1,0,255,255}),"PCM endian/offset damaged");
        check(sound.audio_stats().peak==32768&&sound.audio_stats().nonzero_samples==4,"PCM telemetry");
        check(uint32_t(sound.call("CallIntMethod",{track,write,samples,7,4}))==uint32_t(-2),"PCM range accepted");
        check(uint32_t(sound.call("CallIntMethod",{track,write,samples,UINT32_MAX,4}))==uint32_t(-2),"Negative PCM offset accepted");
        const auto bytes=uint32_t(sound.call("NewByteArray",{8})),nonblock=sound.method(track,"write","([BIII)I");
        queued=100000;bool blocked=false;try{sound.call("CallIntMethod",{track,write,samples,2,4});}catch(const ds::AudioBackpressure&){blocked=true;}
        check(blocked&&captured.size()==8,"Full queue lost or duplicated PCM");
        check(sound.call("CallIntMethod",{track,nonblock,bytes,0,8,1})==0,"Full nonblocking write must return zero");
        queued=0;check(sound.call("CallIntMethod",{track,write,samples,2,4})==4&&captured.size()==16,"PCM retry damaged data");
        queued=44100-4;check(sound.call("CallIntMethod",{track,write,samples,2,4})==2,"250ms PCM cap must return a short partial write");
        check(queued==44100&&sound.audio_stats().buffered_ms()==250,"PCM buffer duration accounting");
        check(sound.call("CallIntMethod",{track,nonblock,bytes,0,8,1})==0,"250ms PCM cap must preserve backpressure");
        check(sound.audio_stats().empty_writes>=1&&sound.audio_stats().backpressure>=3,"PCM starvation/backpressure telemetry");
        sound.call("CallVoidMethod",{track,stop});check(paused&&queued==0,"Audio stop did not clear queue");
        sound.call("CallVoidMethod",{track,sound.method(track,"release","()V")});
        check(uint32_t(sound.call("CallIntMethod",{track,write,samples,2,4}))==uint32_t(-3),"Released track accepted write");
        ds::GuestAudio absent;absent.attach(1,44100,2);reject([&]{absent.play(1);});
        std::cout<<"PASS: PCM JNI dispatch, offsets/endian, output callbacks, bounds, backpressure/retry, lifecycle and missing-device rejection\n";
        std::cout<<"PASS: actual JNI dispatch, all 92 variant slots, ARM32 direct/V/A arguments, UTF-16/MUTF-8, arrays, real asset streams/listing/exceptions\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
