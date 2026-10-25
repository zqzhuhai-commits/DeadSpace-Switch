#include "guest_gl.hpp"
#include "hd_texture.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace ds {
namespace {
constexpr uint32_t vertex=0x8074,normal=0x8075,color=0x8076,texcoord=0x8078;
bool one_of(const std::string& n,std::initializer_list<const char*> names){for(const auto* s:names)if(n==s)return true;return false;}
}
void* GuestGl::proc(const std::string& raw) {
    std::string name=raw;
    if(backend.desktop_compatibility && one_of(name,{"glBindFramebufferOES","glBindRenderbufferOES","glGenFramebuffersOES","glGenRenderbuffersOES","glDeleteFramebuffersOES","glDeleteRenderbuffersOES","glCheckFramebufferStatusOES","glFramebufferTexture2DOES","glFramebufferRenderbufferOES","glRenderbufferStorageOES","glGenerateMipmapOES","glBlendEquationOES","glBlendEquationSeparateOES","glBlendFuncSeparateOES"}))name.resize(name.size()-3);
    auto* p=backend.resolve?backend.resolve(name.c_str()):nullptr;
    if(!p)throw std::runtime_error("Native GL entry unavailable: "+name);
    return p;
}
GuestGl::F GuestGl::number(unsigned n) const {const auto bits=arg(n);F f;std::memcpy(&f,&bits,4);return f;}
size_t GuestGl::checked(uint64_t n){if(n>64*1024*1024)throw std::runtime_error("GL transfer exceeds 64 MiB bound");return size_t(n);}
std::vector<uint8_t> GuestGl::copy(U p,size_t n) {
    checked(n);if(n&&(!p || uint64_t(p)+n>0x100000000ull))throw std::runtime_error("Invalid GL guest range");
    std::vector<uint8_t> bytes(n);if(n)memory.read(p,bytes.data(),n);return bytes;
}
GuestGl::U GuestGl::type_size(U type) {
    if(type==0x1400||type==0x1401)return 1;
    if(type==0x1402||type==0x1403)return 2;
    if(type==0x1406||type==0x140c)return 4;
    throw std::runtime_error("Unsupported GL array type");
}
size_t GuestGl::pixel_bytes(I w,I h,U format,U type,U alignment) {
    if(w<0||h<0||w>4096||h>4096)throw std::runtime_error("GL pixel dimensions bound");
    U channels=format==0x1908?4:format==0x1907?3:format==0x190a?2:(format==0x1906||format==0x1909)?1:0;
    if(!channels)throw std::runtime_error("Unsupported GL pixel format");
    U bytes=0;
    if(type==0x1401)bytes=channels;
    else if((type==0x8363&&format==0x1907)||((type==0x8033||type==0x8034)&&format==0x1908))bytes=2;
    else throw std::runtime_error("Unsupported GL pixel type");
    if(!w||!h)return 0;
    const uint64_t row=uint64_t(w)*bytes,stride=(row+alignment-1)&~uint64_t(alignment-1);
    return checked(stride*uint32_t(h-1)+row); // GL does not read final-row padding
}
GuestGl::U GuestGl::components(U p) {
    switch(p) {
        case 0x0ba6:case 0x0ba7:case 0x0ba8:return 16; // modelview/projection/texture matrices
        case 0x0ba2:case 0x0c10:case 0x0c22:case 0x0c23:case 0x0b00:case 0x0b66:return 4;
        case 0x0d3a:case 0x0b70:case 0x0b12:case 0x0b22:return 2;
        case 0x0d33:case 0x84e2:case 0x0ba0:case 0x0ba3:case 0x0ba4:case 0x0ba5:
        case 0x8069:case 0x8894:case 0x8895:case 0x0cf5:case 0x0d05:
        case 0x8ca6:case 0x8ca7: // framebuffer/renderbuffer binding: one integer
        case 0x0b44:case 0x0b71:case 0x0be2:case 0x0b50:case 0x0b57:case 0x0b74:
        case 0x0b45:case 0x0b46:case 0x0b54:case 0x0bc1:case 0x0bc2:
        case 0x0be0:case 0x0be1:case 0x0b90:case 0x0b91:case 0x0b92:case 0x0b93:
        case 0x0b94:case 0x0b95:case 0x0b96:case 0x0b97:case 0x0b98:
        case 0x0d50:case 0x0d52:case 0x0d53:case 0x0d54:case 0x0d55:case 0x0d56:case 0x0d57:
        case 0x84e0:case 0x84e1:case 0x86a2:return 1;
        default:throw std::runtime_error("Unsupported GL query shape: "+std::to_string(p));
    }
}
GuestGl::Array& GuestGl::array(U n) {
    if(n==texcoord)return arrays[texcoord+client_unit*0x10000];
    if(n==vertex||n==normal||n==color)return arrays[n];
    throw std::runtime_error("Unsupported GL client array");
}
void GuestGl::pointer(const std::string& name,U size,U type,U stride,const void* data) {
    if(name=="glNormalPointer")fn<void(*)(U,I,const void*)>(name)(type,I(stride),data);
    else fn<void(*)(I,U,I,const void*)>(name)(I(size),type,I(stride),data);
}
void GuestGl::prepare_arrays(U last) {
    if(!arrays[vertex].enabled)throw std::runtime_error("GL draw requires enabled vertex array");
    for(auto& entry:arrays) {
        auto& a=entry.second;if(!a.enabled)continue;
        const U slot=entry.first&0xffff,unit=entry.first>>16;
        if(!a.size)throw std::runtime_error("Enabled GL array has no pointer");
        const uint64_t element=uint64_t(a.size)*type_size(a.type),step=a.stride?a.stride:element;
        const size_t length=checked(uint64_t(last)*step+element);
        const void* data;
        if(a.buffer) {
            const auto it=buffers.find(a.buffer);
            if(it==buffers.end() || uint64_t(a.pointer)+length>it->second.size())throw std::runtime_error("GL vertex buffer range exceeded");
            data=reinterpret_cast<const void*>(uintptr_t(a.pointer)); // native GL buffer OFFSET, never host address
        } else {
            a.staging=copy(a.pointer,length);data=a.staging.data();
            if(backend.gpu_client_arrays){
                if(!a.upload_buffer)fn<void(*)(I,U*)>("glGenBuffers")(1,&a.upload_buffer);
                if(!a.upload_buffer)throw std::runtime_error("Client vertex buffer allocation failed");
                fn<void(*)(U,U)>("glBindBuffer")(0x8892,a.upload_buffer);
                // Replace the store so queued draws retain driver-owned
                // storage instead of depending on a reused host vector.
                fn<void(*)(U,intptr_t,const void*,U)>("glBufferData")(0x8892,intptr_t(length),data,0x88e8);
                data=nullptr;++client_array_uploads;
            }
        }
        fn<void(*)(U,U)>("glBindBuffer")(0x8892,a.buffer?a.buffer:(backend.gpu_client_arrays?a.upload_buffer:0));
        if(slot==texcoord)fn<void(*)(U)>("glClientActiveTexture")(0x84c0+unit);
        pointer(slot==vertex?"glVertexPointer":slot==normal?"glNormalPointer":slot==color?"glColorPointer":"glTexCoordPointer",a.size,a.type,a.stride,data);
        if(fn<U(*)()>("glGetError")()!=0)throw std::runtime_error("Native GL rejected vertex-array setup");
    }
    fn<void(*)(U,U)>("glBindBuffer")(0x8892,array_buffer);
    fn<void(*)(U)>("glClientActiveTexture")(0x84c0+client_unit);
}
GuestGl::~GuestGl(){try{
    while(!replacements.empty())invalidate_hd(replacements.begin()->first);
    for(const auto& entry:arrays)if(entry.second.upload_buffer)fn<void(*)(I,const U*)>("glDeleteBuffers")(1,&entry.second.upload_buffer);
    if(client_indices)fn<void(*)(I,const U*)>("glDeleteBuffers")(1,&client_indices);
}catch(...){};}
void GuestGl::invalidate_hd(U texture,bool dynamic) {
    hd_parameters_dirty.erase(texture);
    const auto it=replacements.find(texture);
    if(it!=replacements.end()) {
        fn<void(*)(I,const U*)>("glDeleteTextures")(1,&it->second.shadow);
        hd_live_bytes-=it->second.bytes;replacements.erase(it);
        if(dynamic)++hd_dynamic_fallbacks;
    }
    if(dynamic&&texture)dynamic_textures.insert(texture);
}
GuestGl::U GuestGl::storage_format(U format) {
    if(format==0x8d64||format==0x8c92||format==0x8c00||format==0x8c01)return 0x1907;
    if(format==0x87ee||format==0x8c93||format==0x8c02||format==0x8c03)return 0x1908;
    if(format==0x1906||format==0x1907||format==0x1908||format==0x1909||format==0x190a)return format;
    throw std::runtime_error("Unsupported HD source channel semantics");
}
void GuestGl::upload_rgba(U target,U level,U w,U h,U format,const std::vector<uint8_t>& rgba) {
    if(rgba.size()!=uint64_t(w)*h*4)throw std::runtime_error("Decoded texture size mismatch");
    std::vector<uint8_t> converted;
    const void* pixels=rgba.data();
    if(format!=0x1908) {
        const U channels=format==0x1907?3:format==0x190a?2:1;
        converted.resize(size_t(w)*h*channels);
        for(size_t i=0;i<size_t(w)*h;++i){auto* out=converted.data()+i*channels;const auto* in=rgba.data()+i*4;if(format==0x1907){out[0]=in[0];out[1]=in[1];out[2]=in[2];}else if(format==0x190a){out[0]=in[0];out[1]=in[3];}else out[0]=format==0x1906?in[3]:in[0];}
        pixels=converted.data();
    }
    fn<void(*)(U,I,I,I,I,I,U,U,const void*)>("glTexImage2D")(target,I(level),I(format),I(w),I(h),0,format,0x1401,pixels);
}
void GuestGl::maybe_replace(U target,U level,U w,U h,U format,const std::vector<uint8_t>& pixels) {
    const U original=texture_bindings[active_unit];
    if(!backend.hd_texture||target!=0x0de1||!original||level||dynamic_textures.count(original)||!w||!h)return;
    const auto key=texture_key(w,h,format,pixels);
    if(replacements.count(original) && replacements.at(original).key==key)return;
    invalidate_hd(original);
    if(missing_hd_keys.count(key))return;
    U shadow=0;
    try {
        auto data=backend.hd_texture(key);
        if(data.empty()){++hd_misses;if(missing_hd_keys.size()<4096)missing_hd_keys.insert(key);return;}
        const auto hd=HdTexture::parse(std::move(data));
        if(hd.width<=w || hd.width%w || hd.height%h || hd.width/w!=hd.height/h)throw std::runtime_error("HD texture scale mismatch");
        if(const U error=fn<U(*)()>("glGetError")()) {
            if(pending_errors.size()==16)throw std::runtime_error("GL error queue bound");
            pending_errors.push_back(error);++hd_rejected;last_hd_error="Original GL error before HD upload: "+std::to_string(error);return;
        }
        I maximum=0;fn<void(*)(U,I*)>("glGetIntegerv")(0x0d33,&maximum);
        if(maximum<1||hd.width>U(maximum)||hd.height>U(maximum))throw std::runtime_error("HD dimensions exceed native GL capability");
        uint64_t bytes=0;for(const auto& mip:hd.levels)bytes+=uint64_t(mip.width)*mip.height*4;
        if(hd_live_bytes>backend.hd_budget_bytes || bytes>backend.hd_budget_bytes-hd_live_bytes){++hd_budget_skips;return;}
        // Original texture and its mip chain remain untouched for exact fallback.
        fn<void(*)(I,U*)>("glGenTextures")(1,&shadow);if(!shadow)throw std::runtime_error("HD texture allocation failed");
        fn<void(*)(U,U)>("glBindTexture")(0x0de1,shadow);
        fn<void(*)(U,I)>("glPixelStorei")(0x0cf5,1);
        for(unsigned level_index=0;level_index<hd.levels.size();++level_index) {
            const auto& mip=hd.levels[level_index];const auto rgba=hd.rgba(level_index);
            upload_rgba(0x0de1,level_index,mip.width,mip.height,storage_format(format),rgba);
            if(fn<U(*)()>("glGetError")()!=0)throw std::runtime_error("Native GL rejected HD mip upload");
            ++hd_mip_uploads;if(hd.format==0x87ee)++atc_uploads;
        }
        fn<void(*)(U,I)>("glPixelStorei")(0x0cf5,I(unpack));
        fn<void(*)(U,U)>("glBindTexture")(0x0de1,original);
        replacements.emplace(original,Replacement{shadow,w,h,bytes,key});hd_parameters_dirty.insert(original);hd_live_bytes+=bytes;++hd_matches;
    } catch(const std::exception& e) {
        ++hd_rejected;last_hd_error=std::string(e.what()).substr(0,512);
        if(missing_hd_keys.size()<4096)missing_hd_keys.insert(key);
        if(shadow){fn<void(*)(U,I)>("glPixelStorei")(0x0cf5,I(unpack));fn<void(*)(U,U)>("glBindTexture")(0x0de1,original);fn<void(*)(I,const U*)>("glDeleteTextures")(1,&shadow);}
    }
}
bool GuestGl::limit_exposure(std::array<F,4>& original) {
    const F strength=backend.lighting_strength;
    if(!(strength>=0&&strength<1))return false;
    const auto colors=arrays.find(color);
    if(colors!=arrays.end()&&colors->second.enabled)return false;
    // Track guest state at its setters; avoid driver queries on every opaque
    // draw. The launcher cursor only touches scissor/clear/color-write state.
    if(!blend_enabled||depth_enabled||!textured_units.empty()||blend_source!=0x0306||blend_destination!=0x0303)return false;
    fn<void(*)(U,F*)>("glGetFloatv")(0x0b00,original.data());
    if(original[0]!=1||original[1]!=1||original[2]!=1||!(original[3]>=0&&original[3]<1))return false;
    // These white, untextured exposure quads multiply the destination by
    // 2-alpha. Scale only the increase above 1. Other blend modes, textured
    // surfaces and depth-tested lighting retain their original behavior.
    const F alpha=1-strength*(1-original[3]);
    fn<void(*)(F,F,F,F)>("glColor4f")(1,1,1,alpha);
    ++lighting_limited_draws;return true;
}
void GuestGl::draw_textured(const std::function<void()>& draw) {
    // Broad isolation only: transparent objects and UI can disappear in mode 1.
    // This is not a lighting fix or a filter specific to the player character.
    if(backend.render_test_mode==1&&blend_enabled){
        if(!diagnostic_skipped_draws&&backend.record)backend.record("Render isolation: first blended draw omitted");
        ++diagnostic_skipped_draws;return;
    }
    std::vector<std::pair<U,U>> changed;
    const auto restore=[&]{for(const auto& binding:changed){fn<void(*)(U)>("glActiveTexture")(0x84c0+binding.first);fn<void(*)(U,U)>("glBindTexture")(0x0de1,binding.second);}fn<void(*)(U)>("glActiveTexture")(0x84c0+active_unit);};
    try {
        for(const auto& binding:texture_bindings) {
            const auto it=replacements.find(binding.second);if(it==replacements.end())continue;
            fn<void(*)(U)>("glActiveTexture")(0x84c0+binding.first);
            const bool dirty=hd_parameters_dirty.count(binding.second)!=0;
            I values[4]{};const U names[]={0x2800,0x2801,0x2802,0x2803};
            if(dirty)for(unsigned i=0;i<4;++i)fn<void(*)(U,U,I*)>("glGetTexParameteriv")(0x0de1,names[i],&values[i]);
            changed.push_back(binding);fn<void(*)(U,U)>("glBindTexture")(0x0de1,it->second.shadow);
            if(dirty){for(unsigned i=0;i<4;++i)fn<void(*)(U,U,I)>("glTexParameteri")(0x0de1,names[i],values[i]);hd_parameters_dirty.erase(binding.second);}
        }
        std::array<F,4> original{};const bool limited=limit_exposure(original);
        const auto restore_color=[&]{if(limited)fn<void(*)(F,F,F,F)>("glColor4f")(original[0],original[1],original[2],original[3]);};
        try{
            draw();
            if(backend.render_test_mode==2){
                fn<void(*)()>("glFinish")();
                if(!diagnostic_sync_draws&&backend.record)backend.record("Render isolation: first draw synchronized");
                ++diagnostic_sync_draws;
            }
        }catch(...){restore_color();throw;}
        restore_color();restore();
    } catch(...){restore();throw;}
}
GuestGl::U GuestGl::call(const std::string& name) {
    if(backend.hd_texture&&name.rfind("glTexParameter",0)==0)hd_parameters_dirty.insert(texture_bindings[active_unit]);
    ++calls;const U a=arg(0),b=arg(1),c=arg(2),d=arg(3);
    const auto fixed=[&](unsigned n){return F(I(arg(n)))/65536.0f;};
    if(one_of(name,{"glClearColorx","glColor4x","glRotatex"})){
        const char* target=name=="glClearColorx"?"glClearColor":name=="glColor4x"?"glColor4f":"glRotatef";
        fn<void(*)(F,F,F,F)>(target)(fixed(0),fixed(1),fixed(2),fixed(3));return 0;
    }
    if(one_of(name,{"glTranslatex","glScalex","glNormal3x"})){
        const char* target=name=="glTranslatex"?"glTranslatef":name=="glScalex"?"glScalef":"glNormal3f";
        fn<void(*)(F,F,F)>(target)(fixed(0),fixed(1),fixed(2));return 0;
    }
    if(name=="glLineWidthx"||name=="glPointSizex"){fn<void(*)(F)>(name=="glLineWidthx"?"glLineWidth":"glPointSize")(fixed(0));return 0;}
    if(name=="glClearDepthx"){if(backend.desktop_compatibility)fn<void(*)(double)>("glClearDepth")(fixed(0));else fn<void(*)(F)>("glClearDepthf")(fixed(0));return 0;}
    if(name=="glDepthRangex"){if(backend.desktop_compatibility)fn<void(*)(double,double)>("glDepthRange")(fixed(0),fixed(1));else fn<void(*)(F,F)>("glDepthRangef")(fixed(0),fixed(1));return 0;}
    if(name=="glOrthox"||name=="glFrustumx"){
        if(backend.desktop_compatibility)fn<void(*)(double,double,double,double,double,double)>(name=="glOrthox"?"glOrtho":"glFrustum")(fixed(0),fixed(1),fixed(2),fixed(3),fixed(4),fixed(5));
        else fn<void(*)(F,F,F,F,F,F)>(name=="glOrthox"?"glOrthof":"glFrustumf")(fixed(0),fixed(1),fixed(2),fixed(3),fixed(4),fixed(5));
        return 0;
    }
    if(name=="glLoadMatrixx"||name=="glMultMatrixx"){
        I source[16];F matrix[16];memory.read(a,source,sizeof(source));for(unsigned i=0;i<16;++i)matrix[i]=F(source[i])/65536.0f;
        fn<void(*)(const F*)>(name=="glLoadMatrixx"?"glLoadMatrixf":"glMultMatrixf")(matrix);return 0;
    }
    if(name=="glAlphaFuncx"){fn<void(*)(U,F)>("glAlphaFunc")(a,fixed(1));return 0;}
    if(name=="glPolygonOffsetx"){fn<void(*)(F,F)>("glPolygonOffset")(fixed(0),fixed(1));return 0;}
    if(name=="glSampleCoveragex"){fn<void(*)(F,B)>("glSampleCoverage")(fixed(0),B(b));return 0;}
    if(name=="glMultiTexCoord4x"){fn<void(*)(U,F,F,F,F)>("glMultiTexCoord4f")(a,fixed(1),fixed(2),fixed(3),fixed(4));return 0;}
    if(name=="glFogx"||name=="glLightModelx"||name=="glPointParameterx"){
        const bool enumeration=(name=="glFogx"&&a==0x0b65)||(name=="glLightModelx"&&a==0x0b52);
        fn<void(*)(U,F)>(name=="glFogx"?"glFogf":name=="glLightModelx"?"glLightModelf":"glPointParameterf")(a,enumeration?F(I(b)):fixed(1));return 0;
    }
    if(name=="glLightx"||name=="glMaterialx"){fn<void(*)(U,U,F)>(name=="glLightx"?"glLightf":"glMaterialf")(a,b,fixed(2));return 0;}
    if(name=="glClipPlanex"){
        I raw[4];memory.read(b,raw,sizeof(raw));
        if(backend.desktop_compatibility){double eq[4];for(unsigned i=0;i<4;++i)eq[i]=double(raw[i])/65536;fn<void(*)(U,const double*)>("glClipPlane")(a,eq);}
        else{F eq[4];for(unsigned i=0;i<4;++i)eq[i]=F(raw[i])/65536;fn<void(*)(U,const F*)>("glClipPlanef")(a,eq);}return 0;
    }
    if(one_of(name,{"glFogxv","glLightModelxv","glLightxv","glMaterialxv","glPointParameterxv","glTexEnvxv","glTexParameterxv"})){
        const bool single=name=="glFogxv"||name=="glLightModelxv"||name=="glPointParameterxv";
        const U pname=single?a:b,pointer=single?b:c;U count=0;bool enumeration=false;
        if(name=="glFogxv"){count=pname==0x0b66?4:(pname>=0x0b62&&pname<=0x0b65)?1:0;enumeration=pname==0x0b65;}
        else if(name=="glLightModelxv"){count=pname==0x0b53?4:pname==0x0b52?1:0;enumeration=pname==0x0b52;}
        else if(name=="glLightxv")count=pname>=0x1200&&pname<=0x1203?4:pname==0x1204?3:(pname>=0x1205&&pname<=0x1209)?1:0;
        else if(name=="glMaterialxv")count=pname==0x1601?1:(pname==0x1200||pname==0x1201||pname==0x1202||pname==0x1600||pname==0x1602)?4:0;
        else if(name=="glPointParameterxv")count=pname==0x8129?3:(pname>=0x8126&&pname<=0x8128)?1:0;
        else if(name=="glTexEnvxv"){count=pname==0x2201?4:(pname==0x2200||pname==0x8571||pname==0x8572||pname==0x8573||pname==0x0d1c||pname==0x8590||pname==0x8591||pname==0x8592||pname==0x8580||pname==0x8581||pname==0x8582||pname==0x8588||pname==0x8589||pname==0x858a||pname==0x8598||pname==0x8599||pname==0x859a)?1:0;enumeration=pname!=0x2201&&pname!=0x8573&&pname!=0x0d1c;}
        else {count=pname==0x8b9d?4:(pname>=0x2800&&pname<=0x2803)||pname==0x8191?1:0;enumeration=true;}
        if(!count||!pointer)throw std::runtime_error("Unsupported fixed GL vector shape or pointer");
        std::vector<I> raw(count);std::vector<F> values(count);memory.read(pointer,raw.data(),count*4);
        if(enumeration&&(name=="glTexEnvxv"||name=="glTexParameterxv")){fn<void(*)(U,U,const I*)>(name=="glTexEnvxv"?"glTexEnviv":"glTexParameteriv")(a,b,raw.data());return 0;}
        for(U i=0;i<count;++i)values[i]=enumeration?F(raw[i]):F(raw[i])/65536.0f;
        std::string target=name;target[target.size()-2]='f';
        if(single)fn<void(*)(U,const F*)>(target)(a,values.data());else fn<void(*)(U,U,const F*)>(target)(a,b,values.data());return 0;
    }
    if(name=="glGetError") {if(!pending_errors.empty()){const U error=pending_errors.front();pending_errors.erase(pending_errors.begin());return error;}return fn<U(*)()>(name)();}
    if(name=="glGetString") {
        if(strings.count(a))return strings.at(a);
        const auto* str=fn<const B*(*)(U)>(name)(a);if(!str)return 0;
        std::string s;
        // Advertise no optional extension until its entire guest bridge exists.
        if(a!=0x1f03){for(size_t i=0;i<65536 && str[i];++i)s+=char(str[i]);if(s.size()==65536)throw std::runtime_error("GL string size bound");}
        const U p=memory.alloc(U(s.size()+1));memory.write(p,s.c_str(),s.size()+1);strings[a]=p;return p;
    }
    if(name=="glGetIntegerv"||name=="glGetFloatv"||name=="glGetBooleanv") {
        U count;
        if(a==0x86a3){I n=0;fn<void(*)(U,I*)>("glGetIntegerv")(0x86a2,&n);if(n<0||n>256)throw std::runtime_error("GL format query count bound");count=U(n);}
        else count=components(a);
        if(!b)throw std::runtime_error("Null GL query output");
        if(name=="glGetBooleanv"){std::vector<B> v(count);fn<void(*)(U,B*)>(name)(a,v.data());memory.write(b,v.data(),v.size());}
        else if(name=="glGetFloatv"){std::vector<F> v(count);fn<void(*)(U,F*)>(name)(a,v.data());memory.write(b,v.data(),v.size()*4);}
        else {std::vector<I> v(count);fn<void(*)(U,I*)>(name)(a,v.data());memory.write(b,v.data(),v.size()*4);}
        return 0;
    }
    if(one_of(name,{"glGenTextures","glGenBuffers","glGenFramebuffersOES","glGenRenderbuffersOES","glDeleteTextures","glDeleteBuffers","glDeleteFramebuffersOES","glDeleteRenderbuffersOES"})) {
        if(a>65536 || (a&&!b))throw std::runtime_error("GL object list bound");
        std::vector<U> v(a);
        if(name.rfind("glDelete",0)==0){if(a)memory.read(b,v.data(),a*4);if(name=="glDeleteTextures")for(U id:v){invalidate_hd(id);dynamic_textures.erase(id);for(auto& binding:texture_bindings)if(binding.second==id)binding.second=0;}fn<void(*)(I,const U*)>(name)(I(a),v.data());if(name=="glDeleteBuffers")for(U id:v){buffers.erase(id);index_contents_known.erase(id);if(array_buffer==id)array_buffer=0;if(element_buffer==id)element_buffer=0;for(auto& entry:arrays)if(entry.second.buffer==id){entry.second.pointer=0;entry.second.buffer=0;}}}
        else {fn<void(*)(I,U*)>(name)(I(a),v.data());if(a)memory.write(b,v.data(),a*4);}
        return 0;
    }
    if(name=="glBindBuffer") {if(a!=0x8892&&a!=0x8893)throw std::runtime_error("Unsupported GL buffer target");fn<void(*)(U,U)>(name)(a,b);(a==0x8892?array_buffer:element_buffer)=b;return 0;}
    if(name=="glBufferData"||name=="glBufferSubData") {
        const U id=a==0x8892?array_buffer:a==0x8893?element_buffer:0;
        if(!id)throw std::runtime_error("GL buffer upload without binding");
        if(name=="glBufferData") {
            if(d!=0x88e4 && d!=0x88e8 && !(backend.desktop_compatibility&&d==0x88e0))throw std::runtime_error("Unsupported GL buffer usage");
            checked(b);auto bytes=c?copy(c,b):std::vector<uint8_t>(b);
            fn<void(*)(U,intptr_t,const void*,U)>(name)(a,b,c?bytes.data():nullptr,d);buffers[id]=std::move(bytes);index_contents_known[id]=c!=0;
        } else {
            auto& bytes=buffers.at(id);if(uint64_t(b)+c>bytes.size())throw std::runtime_error("GL buffer subrange exceeded");
            auto update=copy(d,c);fn<void(*)(U,intptr_t,intptr_t,const void*)>(name)(a,b,c,update.data());std::copy(update.begin(),update.end(),bytes.begin()+b);
            if(!b&&c==bytes.size())index_contents_known[id]=true;
        }
        return 0;
    }
    if(name=="glClientActiveTexture") {if(a<0x84c0||a>=0x84c4)throw std::runtime_error("GL client texture unit bound");fn<void(*)(U)>(name)(a);client_unit=a-0x84c0;return 0;}
    if(name=="glEnableClientState"||name=="glDisableClientState") {array(a).enabled=name=="glEnableClientState";fn<void(*)(U)>(name)(a);return 0;}
    if(one_of(name,{"glVertexPointer","glColorPointer","glTexCoordPointer","glNormalPointer"})) {
        const U slot=name=="glVertexPointer"?vertex:name=="glColorPointer"?color:name=="glTexCoordPointer"?texcoord:normal;
        auto& ar=array(slot);const U size=slot==normal?3:a,type=slot==normal?a:b,stride=slot==normal?b:c,p=slot==normal?c:d;
        if(size<2||size>4 || stride>65536)throw std::runtime_error("GL array shape bound");
        if(slot==color){if(size!=4 || (type!=0x1401&&type!=0x1406&&type!=0x140c))throw std::runtime_error("Invalid GL color array shape/type");}
        else if(type!=0x1400&&type!=0x1402&&type!=0x1406&&type!=0x140c)throw std::runtime_error("Invalid GL vertex/normal/texcoord type");
        type_size(type);ar.size=size;ar.type=type;ar.stride=stride;ar.pointer=p;ar.buffer=array_buffer;return 0;
    }
    if(name=="glDrawArrays") {
        if(b>1000000||c>1000000 || uint64_t(b)+c>1000000)throw std::runtime_error("GL vertex count bound");
        if(c)prepare_arrays(b+c-1);
        draw_textured([&]{fn<void(*)(U,I,I)>(name)(a,I(b),I(c));});++draws;return 0;
    }
    if(name=="glDrawElements") {
        if(b>1000000 || (c!=0x1401&&c!=0x1403))throw std::runtime_error("GL index count/type bound");
        const size_t n=checked(uint64_t(b)*type_size(c));std::vector<uint8_t> indices;
        const void* data;
        if(element_buffer){if(b&&!index_contents_known.at(element_buffer))throw std::runtime_error("GL index buffer contents are uninitialized");const auto& shadow=buffers.at(element_buffer);if(uint64_t(d)+n>shadow.size())throw std::runtime_error("GL index buffer range exceeded");indices.assign(shadow.begin()+d,shadow.begin()+d+n);data=reinterpret_cast<const void*>(uintptr_t(d));}
        else {indices=copy(d,n);data=indices.data();}
        U last=0;for(U i=0;i<b;++i){const U value=c==0x1401?indices[i]:U(indices[2*i])|U(indices[2*i+1])<<8;last=std::max(last,value);}
        if(b)prepare_arrays(last);
        const bool upload=backend.gpu_client_arrays&&!element_buffer&&b;
        const auto restore_indices=[&]{if(upload)fn<void(*)(U,U)>("glBindBuffer")(0x8893,element_buffer);};
        try {
            if(upload){
                if(!client_indices)fn<void(*)(I,U*)>("glGenBuffers")(1,&client_indices);
                if(!client_indices)throw std::runtime_error("Client index buffer allocation failed");
                fn<void(*)(U,U)>("glBindBuffer")(0x8893,client_indices);
                fn<void(*)(U,intptr_t,const void*,U)>("glBufferData")(0x8893,intptr_t(n),indices.data(),0x88e8);
                if(fn<U(*)()>("glGetError")()!=0)throw std::runtime_error("Native GL rejected client index upload");
                data=nullptr;++client_index_uploads;
            }
            draw_textured([&]{fn<void(*)(U,I,U,const void*)>(name)(a,I(b),c,data);});
        }catch(...){restore_indices();throw;}
        restore_indices();++draws;return 0;
    }
    if(name=="glPixelStorei") {if((a!=0x0cf5&&a!=0x0d05)||(b!=1&&b!=2&&b!=4&&b!=8))throw std::runtime_error("Unsupported GL pixel-store state");fn<void(*)(U,I)>(name)(a,I(b));(a==0x0cf5?unpack:pack)=b;return 0;}
    if(name=="glTexImage2D"||name=="glTexSubImage2D") {
        const bool sub=name=="glTexSubImage2D";
        const I w=I(arg(sub?4:3)),h=I(arg(sub?5:4));const U format=arg(6),type=arg(7),p=arg(8);
        const auto n=pixel_bytes(w,h,format,type,unpack);auto bytes=p?copy(p,n):std::vector<uint8_t>{};
        if(sub){if(n&&!p)throw std::runtime_error("Null GL texture subimage data");invalidate_hd(texture_bindings[active_unit],true);fn<void(*)(U,I,I,I,I,I,U,U,const void*)>(name)(a,I(b),I(c),I(d),w,h,format,type,bytes.data());}
        else {
            if(!b&&(!p||arg(5)||w<=0||h<=0||!backend.hd_texture))invalidate_hd(texture_bindings[active_unit]);
            fn<void(*)(U,I,I,I,I,I,U,U,const void*)>(name)(a,I(b),I(c),w,h,I(arg(5)),format,type,p?bytes.data():nullptr);
            if(p&&!b&&!arg(5)&&w>0&&h>0&&backend.hd_texture) {
                const auto row=pixel_bytes(w,1,format,type,1),step=(row+unpack-1)&~size_t(unpack-1);
                std::vector<uint8_t> tight(size_t(h)*row);
                for(I y=0;y<h;++y)std::copy_n(bytes.data()+size_t(y)*step,row,tight.data()+size_t(y)*row);
                maybe_replace(a,b,U(w),U(h),format,tight);
            }
        }
        ++texture_uploads;return 0;
    }
    if(name=="glCompressedTexImage2D") {
        const I w=I(d),h=I(arg(4));if(w<1||h<1||w>4096||h>4096||arg(5)!=0)throw std::runtime_error("Compressed GL texture dimensions/border bound");
        const auto bytes=copy(arg(7),checked(arg(6)));
        if(!b&&!backend.hd_texture)invalidate_hd(texture_bindings[active_unit]);
        if(c==0x8d64||c==0x87ee||c==0x8c93||c==0x8c92||(c>=0x8c00&&c<=0x8c03)) {
            const auto rgba=decode_texture_rgba(c,U(w),U(h),bytes);
            fn<void(*)(U,I)>("glPixelStorei")(0x0cf5,1);
            upload_rgba(a,b,U(w),U(h),storage_format(c),rgba);
            fn<void(*)(U,I)>("glPixelStorei")(0x0cf5,I(unpack));if(c==0x87ee||c==0x8c93||c==0x8c92)++atc_uploads;
        } else fn<void(*)(U,I,U,I,I,I,I,const void*)>(name)(a,I(b),c,w,h,0,I(arg(6)),bytes.data());
        if(!b)maybe_replace(a,b,U(w),U(h),c,bytes);
        ++texture_uploads;return 0;
    }
    if(name=="glReadPixels") {
        const U format=arg(4),type=arg(5),p=arg(6);const auto n=pixel_bytes(I(c),I(d),format,type,pack);
        if(n&&!p)throw std::runtime_error("Null GL readback pointer");
        std::vector<B> bytes(n);
        fn<void(*)(I,I,I,I,U,U,void*)>(name)(I(a),I(b),I(c),I(d),format,type,bytes.data());if(n)memory.write(p,bytes.data(),n);return 0;
    }
    if(one_of(name,{"glLoadMatrixf","glMultMatrixf"})) {std::vector<F> v(16);memory.read(a,v.data(),64);fn<void(*)(const F*)>(name)(v.data());return 0;}
    if(one_of(name,{"glFogfv","glLightModelfv","glLightfv","glMaterialfv","glTexEnvfv","glTexParameterfv","glTexEnviv","glTexParameteriv"})) {
        const bool single=name=="glFogfv"||name=="glLightModelfv";
        const U pname=single?a:b,p=single?b:c;U count=0;
        if(name=="glFogfv")count=pname==0x0b66?4:(pname==0x0b62||pname==0x0b63||pname==0x0b64||pname==0x0b65)?1:0;
        else if(name=="glLightModelfv")count=pname==0x0b53?4:pname==0x0b52?1:0;
        else if(name=="glLightfv")count=pname>=0x1200&&pname<=0x1203?4:pname==0x1204?3:pname>=0x1205&&pname<=0x1209?1:0;
        else if(name=="glMaterialfv")count=pname==0x1601?1:(pname==0x1200||pname==0x1201||pname==0x1202||pname==0x1600||pname==0x1602)?4:0;
        else if(name=="glTexEnvfv"||name=="glTexEnviv")count=pname==0x2201?4:(pname==0x2200||pname==0x8571||pname==0x8572||pname==0x8573||pname==0x0d1c||pname==0x8590||pname==0x8591||pname==0x8592||pname==0x8580||pname==0x8581||pname==0x8582||pname==0x8588||pname==0x8589||pname==0x858a||pname==0x8598||pname==0x8599||pname==0x859a)?1:0;
        else count=pname==0x8b9d?4:(pname==0x2800||pname==0x2801||pname==0x2802||pname==0x2803||pname==0x8191)?1:0;
        if(!count||!p)throw std::runtime_error("Unsupported GL vector parameter shape");
        if(name.back()=='v' && name[name.size()-2]=='i'){std::vector<I> v(count);memory.read(p,v.data(),count*4);fn<void(*)(U,U,const I*)>(name)(a,b,v.data());}
        else {std::vector<F> v(count);memory.read(p,v.data(),count*4);if(single)fn<void(*)(U,const F*)>(name)(a,v.data());else fn<void(*)(U,U,const F*)>(name)(a,b,v.data());}
        return 0;
    }
    if(one_of(name,{"glLoadIdentity","glPushMatrix","glPopMatrix","glFinish","glFlush"})){fn<void(*)()>(name)();return 0;}
    if(name=="glOrthof"||name=="glFrustumf") {
        if(backend.desktop_compatibility)fn<void(*)(double,double,double,double,double,double)>(name=="glOrthof"?"glOrtho":"glFrustum")(number(0),number(1),number(2),number(3),number(4),number(5));
        else fn<void(*)(F,F,F,F,F,F)>(name)(number(0),number(1),number(2),number(3),number(4),number(5));
        return 0;
    }
    if(one_of(name,{"glTranslatef","glScalef","glNormal3f"})){fn<void(*)(F,F,F)>(name)(number(0),number(1),number(2));return 0;}
    if(one_of(name,{"glClearColor","glColor4f","glRotatef"})){fn<void(*)(F,F,F,F)>(name)(number(0),number(1),number(2),number(3));return 0;}
    if(name=="glAlphaFunc"){fn<void(*)(U,F)>(name)(a,number(1));return 0;}
    if(name=="glFogf"||name=="glLightModelf"){fn<void(*)(U,F)>(name)(a,number(1));return 0;}
    if(name=="glLightf"||name=="glMaterialf"){fn<void(*)(U,U,F)>(name)(a,b,number(2));return 0;}
    if(name=="glColor4ub"){fn<void(*)(B,B,B,B)>(name)(B(a),B(b),B(c),B(d));return 0;}
    if(name=="glClearDepthf"&&backend.desktop_compatibility){fn<void(*)(double)>("glClearDepth")(number(0));return 0;}
    if(one_of(name,{"glClearDepthf","glLineWidth","glPointSize"})){fn<void(*)(F)>(name)(number(0));return 0;}
    if(name=="glDepthRangef"){if(backend.desktop_compatibility)fn<void(*)(double,double)>("glDepthRange")(number(0),number(1));else fn<void(*)(F,F)>(name)(number(0),number(1));return 0;}
    if(name=="glColorMask"){fn<void(*)(B,B,B,B)>(name)(B(a),B(b),B(c),B(d));return 0;}
    if(name=="glDepthMask"){fn<void(*)(B)>(name)(B(a));return 0;}
    if(one_of(name,{"glViewport","glScissor"})){fn<void(*)(I,I,I,I)>(name)(I(a),I(b),I(c),I(d));return 0;}
    if(one_of(name,{"glTexParameteri","glTexEnvi"})){fn<void(*)(U,U,I)>(name)(a,b,I(c));return 0;}
    if(name=="glTexParameterx"||name=="glTexEnvx"){
        if(!backend.desktop_compatibility)fn<void(*)(U,U,I)>(name)(a,b,I(c));
        else if(name=="glTexParameterx"){
            if(b==0x2800||b==0x2801||b==0x2802||b==0x2803||b==0x8191)fn<void(*)(U,U,I)>("glTexParameteri")(a,b,I(c));
            else throw std::runtime_error("Unsupported fixed texture parameter");
        }else if(b==0x8573||b==0x0d1c)fn<void(*)(U,U,F)>("glTexEnvf")(a,b,F(I(c))/65536.0f);
        else fn<void(*)(U,U,I)>("glTexEnvi")(a,b,I(c));
        return 0;
    }
    if(one_of(name,{"glTexParameterf","glTexEnvf"})){fn<void(*)(U,U,F)>(name)(a,b,number(2));return 0;}
    if(name=="glBindTexture") {if(a!=0x0de1)throw std::runtime_error("Unsupported GL texture target");fn<void(*)(U,U)>(name)(a,b);texture_bindings[active_unit]=b;return 0;}
    if(name=="glActiveTexture") {I units=0;fn<void(*)(U,I*)>("glGetIntegerv")(0x84e2,&units);if(a<0x84c0||a>=0x84c0+std::min(4u,U(std::max(0,units))))throw std::runtime_error("GL active texture unit bound");fn<void(*)(U)>(name)(a);active_unit=a-0x84c0;return 0;}
    if(name=="glEnable"||name=="glDisable"){
        const bool enable=name=="glEnable";
        if(a==0x0be2)blend_enabled=enable;
        if(a==0x0b71)depth_enabled=enable;
        if(a==0x0de1){if(enable)textured_units.insert(active_unit);else textured_units.erase(active_unit);}
        fn<void(*)(U)>(name)(a);return 0;
    }
    if(name=="glBlendFunc"){blend_source=a;blend_destination=b;fn<void(*)(U,U)>(name)(a,b);return 0;}
    if(one_of(name,{"glHint","glBindFramebufferOES","glBindRenderbufferOES","glBlendEquationSeparateOES"})){fn<void(*)(U,U)>(name)(a,b);return 0;}
    if(one_of(name,{"glMatrixMode","glClear","glDepthFunc","glCullFace","glFrontFace","glShadeModel","glStencilMask","glLogicOp","glGenerateMipmapOES","glBlendEquationOES"})){fn<void(*)(U)>(name)(a);return 0;}
    if(name=="glClearStencil"){fn<void(*)(I)>(name)(I(a));return 0;}
    if(name=="glStencilFunc"){fn<void(*)(U,I,U)>(name)(a,I(b),c);return 0;}
    if(name=="glStencilOp"){fn<void(*)(U,U,U)>(name)(a,b,c);return 0;}
    if(name=="glCheckFramebufferStatusOES")return fn<U(*)(U)>(name)(a);
    if(name=="glFramebufferTexture2DOES"){if(d)invalidate_hd(d,true);fn<void(*)(U,U,U,U,I)>(name)(a,b,c,d,I(arg(4)));return 0;}
    if(name=="glFramebufferRenderbufferOES"){fn<void(*)(U,U,U,U)>(name)(a,b,c,d);return 0;}
    if(name=="glRenderbufferStorageOES"){fn<void(*)(U,U,I,I)>(name)(a,b,I(c),I(d));return 0;}
    if(one_of(name,{"glIsTexture","glIsBuffer","glIsEnabled","glIsFramebufferOES","glIsRenderbufferOES"}))return fn<B(*)(U)>(name)(a);
    throw std::runtime_error("Unimplemented native guest GL call: "+name);
}
}
