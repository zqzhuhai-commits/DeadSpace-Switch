#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ds {
struct GraphicsBackend {
    std::function<void*(const char*)> resolve;
    std::function<void()> present;
    bool desktop_compatibility=false; // host reference only; Switch uses GLES1
};
struct GlGuestMemory {
    std::function<uint32_t(unsigned)> arg;
    std::function<uint32_t(uint32_t)> alloc;
    std::function<void(uint32_t,void*,size_t)> read;
    std::function<void(uint32_t,const void*,size_t)> write;
};
// Native GL subset. Unknown operations fail; it never reports a fake success.
class GuestGl {
    using U=uint32_t;using I=int32_t;using F=float;using B=uint8_t;
    GraphicsBackend backend;
    GlGuestMemory memory;
    U array_buffer=0,element_buffer=0,client_unit=0,unpack=4,pack=4;
    struct Array {U size=0,type=0,stride=0,pointer=0,buffer=0;bool enabled=false;std::vector<uint8_t> staging;};
    std::map<U,Array> arrays;
    std::map<U,std::vector<uint8_t>> buffers;
    std::map<U,bool> index_contents_known;
    std::map<U,U> strings;
    void* proc(const std::string& name);
    template<class Signature> Signature fn(const std::string& name){return reinterpret_cast<Signature>(proc(name));}
    U arg(unsigned n) const {return memory.arg(n);}
    F number(unsigned n) const;
    std::vector<uint8_t> copy(U pointer,size_t length);
    static size_t checked(uint64_t length);
    static U components(U pname);
    static U type_size(U type);
    static size_t pixel_bytes(I width,I height,U format,U type,U alignment);
    Array& array(U name);
    void prepare_arrays(U last);
    void pointer(const std::string&,U size,U type,U stride,const void* data);
public:
    GuestGl(GraphicsBackend b,GlGuestMemory m):backend(std::move(b)),memory(std::move(m)){}
    U call(const std::string& name);
    uint64_t calls=0,draws=0,texture_uploads=0,atc_uploads=0;
};
}
