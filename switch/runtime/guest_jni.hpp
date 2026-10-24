#pragma once
#include <cstdint>
#include <stdexcept>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <set>

namespace ds {
struct JavaObject {
    std::string cls, text;
    std::u16string chars;
    std::vector<uint8_t> bytes;
    uint32_t position=0, length=0;
    bool closed=false;
    std::vector<uint32_t> elements;
};
struct MethodKey {
    uint32_t cls;
    std::string name, sig;
    bool operator<(const MethodKey& o) const {
        return cls<o.cls || (cls==o.cls && (name<o.name || (name==o.name && sig<o.sig)));
    }
};
// Shared by the actual lifecycle executor and memory-backed host tests.
// All pointers are guest integers; no JNI argument becomes a native pointer.
class GuestJni {
protected:
    virtual ~GuestJni()=default;
    std::unordered_map<uint32_t,JavaObject> objects;
    std::unordered_map<std::string,uint32_t> classes, singletons;
    std::map<MethodKey,uint32_t> methods;
    std::unordered_map<uint32_t,MethodKey> method_info;
    uint32_t jni_exception=0;
    struct Monitor {uint32_t owner=0,depth=0;};
    std::unordered_map<uint32_t,Monitor> monitors;
    virtual uint32_t current_thread() const {return 1;}
    virtual void block_monitor(uint32_t) {throw std::runtime_error("Contended JNI monitor requires guest scheduler");}
    virtual uint32_t monitor_waiter(uint32_t) const {return 0;}
    virtual void wake_monitor(uint32_t) {throw std::runtime_error("JNI monitor scheduler unavailable");}
    uint64_t asset_opens=0, asset_lists=0, asset_read_bytes=0; unsigned open_log=0;
    std::set<std::string> asset_paths;
    virtual uint32_t alloc(uint32_t)=0;
    virtual uint32_t arg(unsigned)=0;
    virtual uint8_t MemoryRead8(uint32_t)=0;
    virtual uint16_t MemoryRead16(uint32_t)=0;
    virtual uint32_t MemoryRead32(uint32_t)=0;
    virtual uint64_t MemoryRead64(uint32_t)=0;
    virtual void MemoryWrite8(uint32_t,uint8_t)=0;
    virtual void MemoryWrite16(uint32_t,uint16_t)=0;
    virtual void MemoryWrite32(uint32_t,uint32_t)=0;
    virtual void MemoryReadBlock(uint32_t p,void* output,size_t size){auto* bytes=static_cast<uint8_t*>(output);for(size_t i=0;i<size;++i)bytes[i]=MemoryRead8(p+uint32_t(i));}
    virtual void MemoryWriteBlock(uint32_t p,const void* input,size_t size){const auto* bytes=static_cast<const uint8_t*>(input);for(size_t i=0;i<size;++i)MemoryWrite8(p+uint32_t(i),bytes[i]);}
    virtual uint64_t java_delegate(uint32_t,const MethodKey&,const std::vector<uint64_t>&)=0;
    virtual uint32_t guest_pc() const {return 0;}
    std::string last_asset_call;
    JavaObject& object(uint32_t ref);
    std::string cstring(uint32_t address);
    uint32_t java_string(const std::string& modified_utf8);
    uint32_t java_string16(const std::u16string& chars);
    uint32_t find_class(const std::string& name);
    uint32_t singleton(const std::string& name);
    uint32_t raise_exception(const std::string& cls,const std::string& message);
    uint64_t jni_dispatch(const std::string& op,uint32_t env,uint32_t vm);
    static std::vector<char> parameter_types(const std::string& signature);
    std::vector<uint64_t> arguments(const std::string& signature,char mode,unsigned first);
    static uint32_t stride(const JavaObject&);
    std::optional<uint64_t> asset_delegate(uint32_t,const MethodKey&,const std::vector<uint64_t>&,const std::string& root);
};
}
