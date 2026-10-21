#include "guest_jni.hpp"
#include "jni_variant_bindings.hpp"
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

static void check(bool condition,const char* why) {if(!condition)throw std::runtime_error(why);}
template<class F> void reject(F fn) {bool caught=false;try{fn();}catch(const std::exception&){caught=true;}check(caught,"Invalid JNI operation accepted");}
class TestRuntime:public ds::GuestJni {
    std::vector<uint8_t> memory=std::vector<uint8_t>(4*1024*1024);
    uint32_t next=4096;
    std::array<uint32_t,24> words{};
public:
    std::string root;
    uint32_t receiver=0;
    uint32_t thread=1;
    uint32_t current_thread() const override {return thread;}
    std::vector<uint64_t> values;
    static constexpr uint32_t env=256,vm=512;
    uint32_t alloc(uint32_t size) override {
        if(uint64_t(next)+size+16>memory.size())throw std::runtime_error("Guest allocation bound");
        const auto p=next;next+=(size+15)&~15u;if(!size)next+=16;return p;
    }
    uint32_t arg(unsigned n) override {return words.at(n);}
    uint8_t MemoryRead8(uint32_t p) override {return memory.at(p);}
    uint16_t MemoryRead16(uint32_t p) override {return MemoryRead8(p)|uint16_t(MemoryRead8(p+1))<<8;}
    uint32_t MemoryRead32(uint32_t p) override {return MemoryRead16(p)|uint32_t(MemoryRead16(p+2))<<16;}
    uint64_t MemoryRead64(uint32_t p) override {return MemoryRead32(p)|uint64_t(MemoryRead32(p+4))<<32;}
    void MemoryWrite8(uint32_t p,uint8_t v) override {memory.at(p)=v;}
    void MemoryWrite16(uint32_t p,uint16_t v) override {MemoryWrite8(p,uint8_t(v));MemoryWrite8(p+1,uint8_t(v>>8));}
    void MemoryWrite32(uint32_t p,uint32_t v) override {MemoryWrite16(p,uint16_t(v));MemoryWrite16(p+2,uint16_t(v>>16));}
    void put64(uint32_t p,uint64_t v) {MemoryWrite32(p,uint32_t(v));MemoryWrite32(p+4,uint32_t(v>>32));}
    uint64_t java_delegate(uint32_t r,const ds::MethodKey& m,const std::vector<uint64_t>& args) override {
        receiver=r;values=args;
        if(const auto result=asset_delegate(r,m,args,root))return *result;
        if(m.name=="echo")return 0xfedcba9876543210ull;
        if(m.name=="<init>" && m.sig=="(I)V")return 0;
        throw std::runtime_error("Unsupported test delegate");
    }
    uint32_t text(const std::string& s){return java_string(s);}
    uint32_t raw(const std::string& s){const auto p=alloc(uint32_t(s.size()+1));for(uint32_t i=0;i<s.size();++i)MemoryWrite8(p+i,uint8_t(s[i]));return p;}
    uint32_t instance(const std::string& cls){return singleton(cls);}
    uint64_t call(const std::string& op,std::initializer_list<uint32_t> args={}) {
        words.fill(0);words[0]=env;unsigned n=1;for(auto a:args)words.at(n++)=a;
        return jni_dispatch(op,env,vm);
    }
    uint32_t method(uint32_t receiver,const char* name,const char* sig){const auto cls=uint32_t(call("GetObjectClass",{receiver}));return uint32_t(call("GetMethodID",{cls,raw(name),raw(sig)}));}
    std::string string(uint32_t r){return object(r).text;}
    uint32_t data(uint32_t r){return object(r).elements.at(0);}
    uint32_t exception(){return uint32_t(call("ExceptionOccurred"));}
    std::string exception_class(){return object(exception()).cls;}
    uint32_t position(uint32_t r){return object(r).position;}
};
int main() {
    try {
        TestRuntime r;
        std::map<unsigned,std::string> slots;
        ds::register_jni_variants([&](unsigned slot,const char* name){check(slots.emplace(slot,name).second,"Duplicate JNI slot");});
        check(slots.size()==92,"Missing call/array slots");
        check(slots.at(JNI_CallObjectMethodV)=="CallObjectMethodV" && slots.at(JNI_CallStaticLongMethodA)=="CallStaticLongMethodA","Lost V/A suffix");
        check(slots.at(JNI_GetByteArrayElements)=="GetByteArrayElements" && slots.at(JNI_SetDoubleArrayRegion)=="SetDoubleArrayRegion","Missing contiguous array slots");
        const auto same=r.text("same");
        check(r.call("IsSameObject",{same,same})==1 && r.call("IsSameObject",{same,0})==0,"Compared JNIEnv as object");
        const auto chars=r.alloc(12);
        const std::array<uint16_t,5> unicode={u'A',0,0x4e2d,0xd83d,0xde00};
        for(unsigned i=0;i<unicode.size();++i)r.MemoryWrite16(chars+2*i,unicode[i]);
        const auto string=uint32_t(r.call("NewString",{chars,5}));
        check(r.call("GetStringLength",{string})==5 && r.call("GetStringUTFLength",{string})==12,"Wrong UTF-16/modified UTF-8 lengths");
        const auto copy=r.alloc(4);r.MemoryWrite32(copy,0xaabbccdd);
        const auto utf=uint32_t(r.call("GetStringUTFChars",{string,copy}));
        check(r.MemoryRead32(copy)==0xaabbcc01,"isCopy write damaged adjacent memory");
        const auto roundtrip=uint32_t(r.call("NewStringUTF",{utf}));
        check(r.string(roundtrip)==r.string(string),"Modified UTF-8 lost NUL/surrogates");
        const auto utf16=uint32_t(r.call("GetStringChars",{roundtrip,0}));
        for(unsigned i=0;i<unicode.size();++i)check(r.MemoryRead16(utf16+2*i)==unicode[i],"GetStringChars is not UTF-16");
        reject([&]{r.call("NewStringUTF",{r.raw("\xf0\x9f\x98\x80")});});
        const auto array=uint32_t(r.call("NewByteArray",{8}));
        check(r.call("GetArrayLength",{array})==8,"GetArrayLength read JNIEnv");
        const auto source=r.raw("abcd"),destination=r.alloc(8);
        r.call(slots.at(JNI_SetByteArrayRegion),{array,2,4,source});
        r.call(slots.at(JNI_GetByteArrayRegion),{array,2,4,destination});
        check(r.MemoryRead32(source)==r.MemoryRead32(destination),"JNI byte array region copy failed");
        r.call("GetByteArrayRegion",{array,8,0,destination});
        reject([&]{r.call("GetByteArrayRegion",{array,0xffffffff,2,destination});});
        reject([&]{r.call("NewLongArray",{0x40000001});});
        reject([&]{r.call("GetLongArrayElements",{array,0});});
        const auto buffer=r.call("GetByteArrayElements",{array,copy});
        check(buffer==r.data(array) && r.MemoryRead32(copy)==0xaabbcc00,"Array elements pointer/isCopy incorrect");
        const auto cls=uint32_t(r.call("FindClass",{r.raw("java/lang/String")}));
        const auto objects=uint32_t(r.call("NewObjectArray",{2,cls,string}));
        check(r.call("GetObjectArrayElement",{objects,1})==string,"Object array initializer ignored");
        r.call("SetObjectArrayElement",{objects,0,same});
        check(r.call("GetObjectArrayElement",{objects,0})==same,"Object array store incorrect");
        reject([&]{r.call("GetObjectArrayElement",{objects,2});});
        const auto recv=r.instance("test/Receiver");
        check(r.call("MonitorEnter",{recv})==0&&r.call("MonitorEnter",{recv})==0,"Recursive monitor entry failed");
        r.thread=2;reject([&]{r.call("MonitorEnter",{recv});});
        check(uint32_t(r.call("MonitorExit",{recv}))==0xffffffff&&r.exception_class()=="java/lang/IllegalMonitorStateException","Wrong-thread exit accepted");
        r.call("ExceptionClear");r.thread=1;
        check(r.call("MonitorExit",{recv})==0,"Recursive monitor exit failed");
        r.thread=2;reject([&]{r.call("MonitorEnter",{recv});});r.thread=1;
        check(r.call("MonitorExit",{recv})==0,"Final monitor release failed");
        r.thread=2;check(r.call("MonitorEnter",{recv})==0&&r.call("MonitorExit",{recv})==0,"Monitor retained old owner");r.thread=1;
        check(uint32_t(r.call("MonitorEnter",{0}))==0xffffffff&&r.exception_class()=="java/lang/NullPointerException","Null monitor accepted");r.call("ExceptionClear");
        const auto ctor=r.method(recv,"<init>","(I)V");
        const auto receiver_class=uint32_t(r.call("GetObjectClass",{recv}));
        const auto created=uint32_t(r.call("NewObject",{receiver_class,ctor,99}));
        check(created!=receiver_class && r.receiver==created && r.values==std::vector<uint64_t>{99},"Constructor did not initialize instance receiver");
        const auto method=r.method(recv,"echo","([BIJ)J");
        auto result=r.call("CallLongMethod",{recv,method,array,17,0xcccccccc,0x76543210,0xfedcba98});
        check(result==0xfedcba9876543210ull && r.receiver==recv,"Lost receiver/64-bit result");
        check(r.values==std::vector<uint64_t>{array,17,0xfedcba9876543210ull},"Direct AAPCS argument alignment incorrect");
        const auto argv=r.alloc(24);r.put64(argv,array);r.put64(argv+8,17);r.put64(argv+16,0xfedcba9876543210ull);
        r.call("CallLongMethodA",{recv,method,argv});
        check(r.values==std::vector<uint64_t>{array,17,0xfedcba9876543210ull},"jvalue array must use 8-byte slots");
        const auto va=r.alloc(24);r.MemoryWrite32(va,array);r.MemoryWrite32(va+4,17);r.put64(va+8,0xfedcba9876543210ull);
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
        std::cout<<"PASS: actual JNI dispatch, all 92 variant slots, ARM32 direct/V/A arguments, UTF-16/MUTF-8, arrays, real asset streams/listing/exceptions\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
