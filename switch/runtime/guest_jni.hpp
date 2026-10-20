#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

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
    uint64_t asset_opens=0, asset_lists=0, asset_read_bytes=0;
    virtual uint32_t alloc(uint32_t)=0;
    virtual uint32_t arg(unsigned)=0;
    virtual uint8_t MemoryRead8(uint32_t)=0;
    virtual uint16_t MemoryRead16(uint32_t)=0;
    virtual uint32_t MemoryRead32(uint32_t)=0;
    virtual uint64_t MemoryRead64(uint32_t)=0;
    virtual void MemoryWrite8(uint32_t,uint8_t)=0;
    virtual void MemoryWrite16(uint32_t,uint16_t)=0;
    virtual void MemoryWrite32(uint32_t,uint32_t)=0;
    virtual uint64_t java_delegate(uint32_t,const MethodKey&,const std::vector<uint64_t>&)=0;
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
