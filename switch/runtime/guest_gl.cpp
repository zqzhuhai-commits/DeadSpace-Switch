#include "guest_gl.hpp"
#include "../../src/atc_decompress.h"
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
        } else {a.staging=copy(a.pointer,length);data=a.staging.data();}
        fn<void(*)(U,U)>("glBindBuffer")(0x8892,a.buffer);
        if(slot==texcoord)fn<void(*)(U)>("glClientActiveTexture")(0x84c0+unit);
        pointer(slot==vertex?"glVertexPointer":slot==normal?"glNormalPointer":slot==color?"glColorPointer":"glTexCoordPointer",a.size,a.type,a.stride,data);
        if(fn<U(*)()>("glGetError")()!=0)throw std::runtime_error("Native GL rejected vertex-array setup");
    }
    fn<void(*)(U,U)>("glBindBuffer")(0x8892,array_buffer);
    fn<void(*)(U)>("glClientActiveTexture")(0x84c0+client_unit);
}
GuestGl::U GuestGl::call(const std::string& name) {
    ++calls;const U a=arg(0),b=arg(1),c=arg(2),d=arg(3);
    if(name=="glGetError")return fn<U(*)()>(name)();
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
        if(name.rfind("glDelete",0)==0){if(a)memory.read(b,v.data(),a*4);fn<void(*)(I,const U*)>(name)(I(a),v.data());if(name=="glDeleteBuffers")for(U id:v){buffers.erase(id);index_contents_known.erase(id);if(array_buffer==id)array_buffer=0;if(element_buffer==id)element_buffer=0;for(auto& entry:arrays)if(entry.second.buffer==id){entry.second.pointer=0;entry.second.buffer=0;}}}
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
        fn<void(*)(U,I,I)>(name)(a,I(b),I(c));++draws;return 0;
    }
    if(name=="glDrawElements") {
        if(b>1000000 || (c!=0x1401&&c!=0x1403))throw std::runtime_error("GL index count/type bound");
        const size_t n=checked(uint64_t(b)*type_size(c));std::vector<uint8_t> indices;
        const void* data;
        if(element_buffer){if(b&&!index_contents_known.at(element_buffer))throw std::runtime_error("GL index buffer contents are uninitialized");const auto& shadow=buffers.at(element_buffer);if(uint64_t(d)+n>shadow.size())throw std::runtime_error("GL index buffer range exceeded");indices.assign(shadow.begin()+d,shadow.begin()+d+n);data=reinterpret_cast<const void*>(uintptr_t(d));}
        else {indices=copy(d,n);data=indices.data();}
        U last=0;for(U i=0;i<b;++i){const U value=c==0x1401?indices[i]:U(indices[2*i])|U(indices[2*i+1])<<8;last=std::max(last,value);}
        if(b)prepare_arrays(last);
        fn<void(*)(U,I,U,const void*)>(name)(a,I(b),c,data);++draws;return 0;
    }
    if(name=="glPixelStorei") {if((a!=0x0cf5&&a!=0x0d05)||(b!=1&&b!=2&&b!=4&&b!=8))throw std::runtime_error("Unsupported GL pixel-store state");fn<void(*)(U,I)>(name)(a,I(b));(a==0x0cf5?unpack:pack)=b;return 0;}
    if(name=="glTexImage2D"||name=="glTexSubImage2D") {
        const bool sub=name=="glTexSubImage2D";
        const I w=I(arg(sub?4:3)),h=I(arg(sub?5:4));const U format=arg(6),type=arg(7),p=arg(8);
        const auto n=pixel_bytes(w,h,format,type,unpack);auto bytes=p?copy(p,n):std::vector<uint8_t>{};
        if(sub){if(n&&!p)throw std::runtime_error("Null GL texture subimage data");fn<void(*)(U,I,I,I,I,I,U,U,const void*)>(name)(a,I(b),I(c),I(d),w,h,format,type,bytes.data());}
        else fn<void(*)(U,I,I,I,I,I,U,U,const void*)>(name)(a,I(b),I(c),w,h,I(arg(5)),format,type,p?bytes.data():nullptr);
        ++texture_uploads;return 0;
    }
    if(name=="glCompressedTexImage2D") {
        const I w=I(d),h=I(arg(4));if(w<1||h<1||w>4096||h>4096||arg(5)!=0)throw std::runtime_error("Compressed GL texture dimensions/border bound");
        const auto bytes=copy(arg(7),checked(arg(6)));
        if(c==0x87ee||c==0x8c93||c==0x8c92) {
            std::vector<B> rgba(checked(uint64_t(w)*h*4));
            const bool ok=c==0x87ee?atc::decode_rgba_interpolated(bytes.data(),bytes.size(),w,h,rgba.data()):c==0x8c93?atc::decode_rgba_explicit(bytes.data(),bytes.size(),w,h,rgba.data()):atc::decode_rgb(bytes.data(),bytes.size(),w,h,rgba.data());
            if(!ok)throw std::runtime_error("Invalid ATC compressed GL texture");
            fn<void(*)(U,I)>("glPixelStorei")(0x0cf5,1);
            fn<void(*)(U,I,I,I,I,I,U,U,const void*)>("glTexImage2D")(a,I(b),0x1908,w,h,0,0x1908,0x1401,rgba.data());
            fn<void(*)(U,I)>("glPixelStorei")(0x0cf5,I(unpack));++atc_uploads;
        } else fn<void(*)(U,I,U,I,I,I,I,const void*)>(name)(a,I(b),c,w,h,0,I(arg(6)),bytes.data());
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
    if(one_of(name,{"glTexParameterf","glTexEnvf"})){fn<void(*)(U,U,F)>(name)(a,b,number(2));return 0;}
    if(one_of(name,{"glBindTexture","glBlendFunc","glHint","glBindFramebufferOES","glBindRenderbufferOES","glBlendEquationSeparateOES"})){fn<void(*)(U,U)>(name)(a,b);return 0;}
    if(one_of(name,{"glActiveTexture","glMatrixMode","glClear","glEnable","glDisable","glDepthFunc","glCullFace","glFrontFace","glShadeModel","glStencilMask","glLogicOp","glGenerateMipmapOES","glBlendEquationOES"})){fn<void(*)(U)>(name)(a);return 0;}
    if(name=="glClearStencil"){fn<void(*)(I)>(name)(I(a));return 0;}
    if(name=="glStencilFunc"){fn<void(*)(U,I,U)>(name)(a,I(b),c);return 0;}
    if(name=="glStencilOp"){fn<void(*)(U,U,U)>(name)(a,b,c);return 0;}
    if(name=="glCheckFramebufferStatusOES")return fn<U(*)(U)>(name)(a);
    if(name=="glFramebufferTexture2DOES"){fn<void(*)(U,U,U,U,I)>(name)(a,b,c,d,I(arg(4)));return 0;}
    if(name=="glFramebufferRenderbufferOES"){fn<void(*)(U,U,U,U)>(name)(a,b,c,d);return 0;}
    if(name=="glRenderbufferStorageOES"){fn<void(*)(U,U,I,I)>(name)(a,b,I(c),I(d));return 0;}
    if(one_of(name,{"glIsTexture","glIsBuffer","glIsEnabled","glIsFramebufferOES","glIsRenderbufferOES"}))return fn<B(*)(U)>(name)(a);
    throw std::runtime_error("Unimplemented native guest GL call: "+name);
}
}
