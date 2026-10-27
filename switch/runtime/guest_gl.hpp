#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "guest_input.hpp"

namespace ds {
struct GraphicsBackend {
    std::function<void*(const char*)> resolve;
    std::function<void()> present;
    // Called on the emulation thread, between CPU runs. No invented percentage:
    // byte/frame counters describe work observed, not an assumed total.
    std::function<void(const std::string&,uint64_t,uint64_t)> loading;
    std::function<void()> startup_complete;
    bool desktop_compatibility=false; // host reference only; Switch uses GLES1
    bool realtime_clock=false;
    bool gpu_client_arrays=false; // Upload client arrays into driver-owned buffer stores
    float lighting_strength=1.0f; // 1 preserves original untextured exposure passes
    uint32_t surface_width=640,surface_height=480;
    std::function<std::vector<uint8_t>(const std::string&)> hd_texture={};
    uint64_t hd_budget_bytes=128*1024*1024;
    std::function<void(bool)> audio_playing;
    // Samples are signed 16-bit interleaved stereo at 44100 Hz. No value
    // means the real device queue is full: suspend this guest worker.
    std::function<std::optional<uint32_t>(const std::vector<int16_t>&)> audio_write;
    std::function<std::vector<GuestInput>()> poll_input;
    std::function<void(bool)> input_mode;
    std::function<void(const std::string&)> record;
    bool base_mip_only=false;
    unsigned render_test_mode=0;
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
    // Entries belong to this context and die with it; never cache across contexts.
    std::map<std::string,void*> procedures;
    std::vector<uint8_t> index_staging;
    U array_buffer=0,element_buffer=0,client_unit=0,unpack=4,pack=4,client_indices=0;
    struct Array {U size=0,type=0,stride=0,pointer=0,buffer=0,upload_buffer=0;bool enabled=false;std::vector<uint8_t> staging;};
    std::map<U,Array> arrays;
    std::map<U,std::vector<uint8_t>> buffers;
    std::map<U,bool> index_contents_known;
    std::map<U,U> strings;
    U active_unit=0;
    bool blend_enabled=false,depth_enabled=false;
    U blend_source=1,blend_destination=0;
    std::set<U> textured_units;
    std::map<U,U> texture_bindings;
    // Guest setters/deletion invalidate this lazily queried filter state.
    std::map<U,I> min_filters;
    struct Replacement {U shadow,width,height;uint64_t bytes;std::string key;};
    std::map<U,Replacement> replacements;
    std::set<U> hd_parameters_dirty;
    std::set<U> dynamic_textures;
    std::set<std::string> missing_hd_keys;
    uint64_t hd_live_bytes=0;
    std::vector<U> pending_errors;
    void* proc(const std::string& name);
    template<class Signature> Signature fn(const std::string& name){return reinterpret_cast<Signature>(proc(name));}
    U arg(unsigned n) const {return memory.arg(n);}
    F number(unsigned n) const;
    std::vector<uint8_t> copy(U pointer,size_t length);
    void copy_into(U pointer,size_t length,std::vector<uint8_t>& bytes);
    static size_t checked(uint64_t length);
    static U components(U pname);
    static U type_size(U type);
    static size_t pixel_bytes(I width,I height,U format,U type,U alignment);
    Array& array(U name);
    void prepare_arrays(U last);
    void pointer(const std::string&,U size,U type,U stride,const void* data);
    void invalidate_hd(U texture,bool dynamic=false);
    void maybe_replace(U target,U level,U width,U height,U format,const std::vector<uint8_t>& pixels);
    bool limit_exposure(std::array<F,4>& original);
    void draw_textured(const std::function<void()>& draw);
    static U storage_format(U original_format);
    void upload_rgba(U target,U level,U width,U height,U format,const std::vector<uint8_t>& rgba);
public:
    GuestGl(GraphicsBackend b,GlGuestMemory m):backend(std::move(b)),memory(std::move(m)){}
    ~GuestGl();
    U call(const std::string& name);
    uint64_t diagnostic_skipped_draws=0,diagnostic_sync_draws=0;
    uint64_t calls=0,draws=0,texture_uploads=0,atc_uploads=0,lighting_limited_draws=0,client_array_uploads=0,client_index_uploads=0;
    uint64_t hd_matches=0,hd_rejected=0,hd_misses=0,hd_budget_skips=0,hd_dynamic_fallbacks=0,hd_mip_uploads=0;
    std::string last_hd_error;
};
}
