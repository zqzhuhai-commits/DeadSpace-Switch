#include "guest_jni.hpp"
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <dirent.h>
#include <memory>
#include <limits>
#include <stdexcept>

namespace ds {
namespace {
constexpr uint32_t limit=32*1024*1024;
std::string encode(const std::u16string& s) {
    std::string out;
    for(uint16_t c:s) {
        if(c && c<0x80)out+=char(c);
        else if(c<0x800){out+=char(0xc0|(c>>6));out+=char(0x80|(c&63));}
        else {out+=char(0xe0|(c>>12));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63));}
    }
    return out;
}
std::u16string decode(const std::string& s) {
    std::u16string out;
    for(size_t i=0;i<s.size();) {
        const auto c=uint8_t(s[i++]);
        if(c && c<0x80){out+=char16_t(c);continue;}
        unsigned count=(c&0xe0)==0xc0?1:(c&0xf0)==0xe0?2:0;
        if(!count || i+count>s.size())throw std::runtime_error("Invalid modified UTF-8");
        uint16_t v=c&(count==1?31:15);
        for(unsigned n=0;n<count;++n){const auto b=uint8_t(s[i++]);if((b&0xc0)!=0x80)throw std::runtime_error("Invalid modified UTF-8 continuation");v=uint16_t((v<<6)|(b&63));}
        if((count==1 && v<128 && v!=0) || (count==2 && v<0x800))throw std::runtime_error("Overlong modified UTF-8");
        out+=char16_t(v);
    }
    return out;
}
uint32_t advance(uint32_t p,uint32_t n) {
    if(p>UINT32_MAX-n)throw std::runtime_error("JNI guest pointer overflow");
    return p+n;
}
void range(uint32_t off,uint32_t count,uint32_t length) {
    if(off>length || count>length-off)throw std::runtime_error("JNI array region outside array");
}
}
JavaObject& GuestJni::object(uint32_t ref) {
    const auto it=objects.find(ref);
    if(!ref || it==objects.end())throw std::runtime_error("Invalid JNI object reference");
    return it->second;
}
std::string GuestJni::cstring(uint32_t p) {
    if(!p)throw std::runtime_error("Null JNI string pointer");
    std::string s;
    for(uint32_t i=0;i<1024*1024;++i){const auto c=MemoryRead8(advance(p,i));if(!c)return s;s+=char(c);}
    throw std::runtime_error("Unterminated JNI string");
}
uint32_t GuestJni::java_string(const std::string& s) {return java_string16(decode(s));}
uint32_t GuestJni::java_string16(const std::u16string& chars) {
    if(chars.size()>limit/2)throw std::runtime_error("JNI string size bound");
    const auto ref=alloc(4);auto& o=objects[ref];o.cls="java/lang/String";o.chars=chars;o.text=encode(chars);return ref;
}
uint32_t GuestJni::find_class(const std::string& name) {
    auto& ref=classes[name];if(!ref){ref=alloc(4);objects[ref].cls=name;}return ref;
}
uint32_t GuestJni::singleton(const std::string& name) {
    auto& ref=singletons[name];if(!ref){ref=alloc(4);objects[ref].cls=name;}return ref;
}
uint32_t GuestJni::raise_exception(const std::string& cls,const std::string& message) {
    if(!jni_exception){jni_exception=alloc(4);objects[jni_exception].cls=cls;objects[jni_exception].text=message;}
    return 0;
}
uint32_t GuestJni::stride(const JavaObject& o) {
    if(o.cls=="[Z"||o.cls=="[B")return 1;
    if(o.cls=="[C"||o.cls=="[S")return 2;
    if(o.cls=="[I"||o.cls=="[F")return 4;
    if(o.cls=="[J"||o.cls=="[D")return 8;
    throw std::runtime_error("JNI operation requires primitive array");
}
std::optional<uint64_t> GuestJni::asset_delegate(uint32_t receiver,const MethodKey& m,const std::vector<uint64_t>& values,const std::string& root) {
    auto& o=object(receiver);
    if(o.cls=="android/content/res/AssetManager" && (m.name=="open"||m.name=="openFd"||m.name=="list")) {
        std::string name=object(uint32_t(values.at(0))).text;
        if(name.rfind("appbundle:",0)==0)name=name.substr(10);
        if(name.find_first_of("\\:\0",0,3)!=std::string::npos || (!name.empty()&&name[0]=='/'))return raise_exception("java/io/IOException","Unsafe asset path");
        size_t p=0;
        while(p<=name.size()){const auto end=name.find('/',p);const auto part=name.substr(p,end==std::string::npos?end:end-p);if(part==".."||part==".")return raise_exception("java/io/IOException","Unsafe asset path component");if(end==std::string::npos)break;p=end+1;}
        const auto path=root+"/assets/"+name;
        if(m.name=="list") {
            using Directory=std::unique_ptr<DIR,int(*)(DIR*)>;
            Directory directory(opendir(path.c_str()),closedir);
            if(!directory)return raise_exception("java/io/IOException","Cannot list asset directory: "+name);
            std::vector<std::string> names;
            while(auto entry=readdir(directory.get())){std::string n=entry->d_name;if(n!="."&&n!=".."){if(names.size()==10000)return raise_exception("java/io/IOException","Asset directory size bound");names.push_back(n);}}
            std::sort(names.begin(),names.end());
            const auto ref=alloc(4);auto& array=objects[ref];array.cls="[Ljava/lang/String;";array.length=uint32_t(names.size());
            for(const auto& n:names)array.elements.push_back(java_string(n));
            ++asset_lists;
            return ref;
        }
        using File=std::unique_ptr<std::FILE,int(*)(std::FILE*)>;
        File file(std::fopen(path.c_str(),"rb"),std::fclose);
        if(!file)return raise_exception("java/io/FileNotFoundException",name);
        if(std::fseek(file.get(),0,SEEK_END)!=0)return raise_exception("java/io/IOException","Asset seek failed");
        const long size=std::ftell(file.get());
        if(size<0 || uint64_t(size)>64*1024*1024 || std::fseek(file.get(),0,SEEK_SET)!=0)return raise_exception("java/io/IOException","Asset size/seek bound");
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        if(!bytes.empty() && std::fread(bytes.data(),1,bytes.size(),file.get())!=bytes.size())return raise_exception("java/io/IOException","Asset read failed");
        const auto ref=alloc(4);objects[ref].cls=m.name=="open"?"java/io/InputStream":"android/content/res/AssetFileDescriptor";objects[ref].bytes=std::move(bytes);++asset_opens;return ref;
    }
    if(o.cls!="java/io/InputStream" && o.cls!="android/content/res/AssetFileDescriptor")return std::nullopt;
    if(m.name=="close"){o.closed=true;return 0;}
    if(o.closed)return raise_exception("java/io/IOException","Asset stream is closed");
    if(m.name=="getLength")return o.bytes.size();
    if(m.name=="available")return o.bytes.size()-o.position;
    if(m.name=="skip") {
        const int64_t requested=int64_t(values.at(0));
        const auto n=requested<=0?0:std::min<uint64_t>(uint64_t(requested),o.bytes.size()-o.position);
        o.position+=uint32_t(n);return n;
    }
    if(m.name=="read" && m.sig=="()I") {
        if(o.position==o.bytes.size())return uint32_t(-1);
        ++asset_read_bytes;return o.bytes[o.position++];
    }
    if(m.name=="read" && (m.sig=="([BII)I" || m.sig=="([B)I")) {
        auto& array=object(uint32_t(values.at(0)));
        if(array.cls!="[B")return raise_exception("java/lang/IllegalArgumentException","Expected byte array");
        const uint32_t off=m.sig=="([B)I"?0:uint32_t(values.at(1)),count=m.sig=="([B)I"?array.length:uint32_t(values.at(2));
        if(off>array.length || count>array.length-off)return raise_exception("java/lang/IndexOutOfBoundsException","Asset read outside array");
        if(!count)return 0; // zero-sized read also returns zero at EOF
        const uint32_t n=uint32_t(std::min<uint64_t>(count,o.bytes.size()-o.position));
        for(uint32_t i=0;i<n;++i)MemoryWrite8(array.elements.at(0)+off+i,o.bytes[o.position+i]);
        o.position+=n;asset_read_bytes+=n;return n?n:uint32_t(-1);
    }
    return std::nullopt;
}
std::vector<char> GuestJni::parameter_types(const std::string& sig) {
    size_t p=0;std::vector<char> types;
    auto type=[&](bool allow_void) {
        if(p>=sig.size())throw std::runtime_error("Truncated JNI descriptor");
        char t=sig[p++];bool array=false;
        while(t=='['){array=true;if(p>=sig.size())throw std::runtime_error("Truncated JNI array descriptor");t=sig[p++];}
        if(t=='L'){const auto end=sig.find(';',p);if(end==std::string::npos||end==p)throw std::runtime_error("Invalid JNI object descriptor");p=end+1;}
        else if(std::string("ZBCSIJFD").find(t)==std::string::npos && !(allow_void&&!array&&t=='V'))throw std::runtime_error("Invalid JNI descriptor type");
        return array?'L':t;
    };
    if(sig.size()>4096 || sig.empty() || sig[p++]!='(')throw std::runtime_error("Invalid JNI method descriptor");
    while(p<sig.size()&&sig[p]!=')'){if(types.size()==256)throw std::runtime_error("JNI argument limit");types.push_back(type(false));}
    if(p>=sig.size()||sig[p++]!=')')throw std::runtime_error("Missing JNI return descriptor");
    type(true);if(p!=sig.size())throw std::runtime_error("Trailing JNI descriptor");return types;
}
std::vector<uint64_t> GuestJni::arguments(const std::string& sig,char mode,unsigned first) {
    std::vector<uint64_t> result;
    uint32_t p=mode=='D'?0:arg(first);
    unsigned word=first;
    for(char t:parameter_types(sig)) {
        const bool wide=t=='J'||t=='D', promoted=t=='F'&&mode!='A';
        uint64_t value;
        if(mode=='A') {
            value=(wide?MemoryRead64(p):MemoryRead32(p));p=advance(p,8); // every jvalue occupies 8 bytes
        } else if(mode=='V') {
            if(wide||promoted){p=advance(p,7)&~7u;value=MemoryRead64(p);p=advance(p,8);}
            else{value=MemoryRead32(p);p=advance(p,4);}
        } else {
            if(wide||promoted)word=(word+1)&~1u;
            value=arg(word++);if(wide||promoted){value|=uint64_t(arg(word++))<<32;}
        }
        if(promoted){double d;std::memcpy(&d,&value,8);const float f=float(d);uint32_t bits;std::memcpy(&bits,&f,4);value=bits;}
        if(t=='Z')value=value?1:0;
        else if(t=='B')value=uint32_t(int32_t(int8_t(value)));
        else if(t=='C')value=uint16_t(value);
        else if(t=='S')value=uint32_t(int32_t(int16_t(value)));
        result.push_back(value);
    }
    return result;
}
uint64_t GuestJni::jni_dispatch(const std::string& op,uint32_t env,uint32_t vm) {
    if(arg(0)!=env)throw std::runtime_error("Invalid JNIEnv argument");
    const uint32_t b=arg(1),c=arg(2);
    if(op=="GetVersion")return 0x10006;
    if(op=="FindClass")return find_class(cstring(b));
    if(op=="GetMethodID"||op=="GetStaticMethodID") {
        object(b);MethodKey k{b,cstring(c),cstring(arg(3))};parameter_types(k.sig);
        auto& id=methods[k];if(!id){id=alloc(4);method_info[id]=k;}return id;
    }
    if(op=="NewLocalRef"||op=="NewGlobalRef"){if(b)object(b);return b;}
    // Handles remain stable until this diagnostic runtime is destroyed. Scoped
    // reference reclamation is deliberately not advertised as implemented.
    if(op=="DeleteLocalRef"||op=="DeleteGlobalRef")return 0;
    if(op=="ExceptionClear"){jni_exception=0;return 0;}
    if(op=="ExceptionCheck")return jni_exception?1:0;
    if(op=="ExceptionOccurred")return jni_exception;
    if(op=="Throw"){if(b)object(b);jni_exception=b;return 0;}
    if(op=="ThrowNew")return raise_exception(object(b).cls,cstring(c));
    if(op=="GetObjectClass")return find_class(object(b).cls);
    if(op=="IsSameObject")return b==c;
    if(op=="GetJavaVM"){MemoryWrite32(b,vm);return 0;}
    if(op=="PushLocalFrame"||op=="EnsureLocalCapacity"){if(int32_t(b)<0)throw std::runtime_error("Negative JNI local capacity");return 0;}
    if(op=="PopLocalFrame")return b;
    if(op=="NewObject"||op=="NewObjectV"||op=="NewObjectA") {
        const auto cls=object(b).cls;
        const auto m=method_info.at(c);
        if(m.cls!=b || m.name!="<init>" || m.sig.back()!='V')throw std::runtime_error("Invalid JNI constructor");
        const char mode=op.back()=='A'?'A':op.back()=='V'?'V':'D';
        const auto values=arguments(m.sig,mode,3);
        const auto ref=alloc(4);objects[ref].cls=cls;
        java_delegate(ref,m,values);
        return jni_exception?0:ref;
    }
    if(op=="NewStringUTF")return java_string(cstring(b));
    if(op=="NewString") {
        if(c>limit/2)throw std::runtime_error("JNI string length bound");
        std::u16string s;for(uint32_t i=0;i<c;++i)s+=char16_t(MemoryRead16(advance(b,i*2)));return java_string16(s);
    }
    if(op=="GetStringLength"||op=="GetStringUTFLength") {
        const auto& o=object(b);if(o.cls!="java/lang/String")throw std::runtime_error("Expected JNI string");
        return uint32_t(op=="GetStringLength"?o.chars.size():o.text.size());
    }
    if(op=="GetStringChars"||op=="GetStringUTFChars") {
        const auto& o=object(b);if(o.cls!="java/lang/String")throw std::runtime_error("Expected JNI string");
        const bool utf=op=="GetStringUTFChars";
        const auto p=alloc(uint32_t(utf?o.text.size()+1:(o.chars.size()+1)*2));
        if(c)MemoryWrite8(c,1); // copied into guest-owned storage
        if(utf){for(uint32_t i=0;i<o.text.size();++i)MemoryWrite8(p+i,uint8_t(o.text[i]));MemoryWrite8(p+uint32_t(o.text.size()),0);}
        else {for(uint32_t i=0;i<o.chars.size();++i)MemoryWrite16(p+i*2,uint16_t(o.chars[i]));MemoryWrite16(p+uint32_t(o.chars.size())*2,0);}
        return p;
    }
    if(op=="ReleaseStringChars"||op=="ReleaseStringUTFChars")return 0;
    if(op=="GetArrayLength") {const auto& o=object(b);if(o.cls.empty()||o.cls[0]!='[')throw std::runtime_error("Expected JNI array");return o.length;}
    if(op=="NewObjectArray") {
        if(b>limit/4)throw std::runtime_error("JNI object array size bound");
        const auto cls=object(c).cls;if(arg(3))object(arg(3));
        const auto ref=alloc(4);auto& o=objects[ref];o.cls="[L"+cls+";";o.length=b;o.elements.assign(b,arg(3));return ref;
    }
    if(op=="GetObjectArrayElement"||op=="SetObjectArrayElement") {
        auto& o=object(b);if(o.cls.rfind("[L",0)!=0)throw std::runtime_error("Expected JNI object array");range(c,1,o.length);
        if(op=="GetObjectArrayElement")return o.elements.at(c);
        if(arg(3))object(arg(3));
        o.elements.at(c)=arg(3);return 0;
    }
    const std::vector<std::string> names={"Boolean","Byte","Char","Short","Int","Long","Float","Double"};
    for(size_t i=0;i<names.size();++i) {
        if(op=="New"+names[i]+"Array") {
            const auto ref=alloc(4);JavaObject o;o.cls=std::string("[")+"ZBCSIJFD"[i];o.length=b;
            const uint32_t step=stride(o);if(b>limit/step)throw std::runtime_error("JNI array allocation bound");
            o.elements={alloc(b*step)};objects[ref]=std::move(o);return ref;
        }
        if(op=="Get"+names[i]+"ArrayElements" || op=="Release"+names[i]+"ArrayElements" || op=="Get"+names[i]+"ArrayRegion" || op=="Set"+names[i]+"ArrayRegion") {
            auto& o=object(b);if(o.cls!=std::string("[")+"ZBCSIJFD"[i])throw std::runtime_error("JNI array type mismatch");
            if(op.find("ArrayElements")!=std::string::npos) {
                if(op.rfind("Release",0)==0){if(c!=o.elements.at(0)||arg(3)>2)throw std::runtime_error("Invalid JNI array release");return 0;}
                if(c)MemoryWrite8(c,0);
                return o.elements.at(0);
            }
            const uint32_t count=arg(3),p=arg(4),step=stride(o);range(c,count,o.length);
            const bool set=op.rfind("Set",0)==0;
            for(uint32_t n=0;n<count*step;++n) {
                const uint32_t element=o.elements.at(0)+c*step+n, buffer=advance(p,n);
                MemoryWrite8(set?element:buffer,MemoryRead8(set?buffer:element));
            }
            return 0;
        }
    }
    if(op.rfind("Call",0)==0 && op.find("Method")!=std::string::npos) {
        if(op.find("Nonvirtual")!=std::string::npos)throw std::runtime_error("Nonvirtual JNI call unsupported");
        const auto m=method_info.at(c);object(b);
        const char mode=op.back()=='A'?'A':op.back()=='V'?'V':'D';
        const auto values=arguments(m.sig,mode,3);
        const uint64_t result=java_delegate(b,m,values); // receiver, never declaring class
        // ARM32 soft-float ABI returns jlong/jdouble in r0:r1. The caller writes r1.
        return result;
    }
    throw std::runtime_error("Unsupported JNI: "+op);
}
}
