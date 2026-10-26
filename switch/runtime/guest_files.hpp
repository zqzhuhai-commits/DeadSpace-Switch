#pragma once
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <map>
#include <memory>
#include <sys/stat.h>

namespace ds {
// Guest FILE* values are opaque 32-bit tokens, never narrowed host pointers.
// Descriptor and stdio namespaces are distinct; closed tokens are not reused.
class GuestFiles {
    struct Closer {void operator()(std::FILE* fp)const{std::fclose(fp);}};
    using File = std::unique_ptr<std::FILE, Closer>;
    std::map<uint32_t, File> descriptors, streams;
    uint32_t next_fd=3, next_stream=0x71000000;
    uint32_t insert(std::map<uint32_t, File>& table,uint32_t& next,
                    const char* path,const char* mode,uint32_t failure) {
        if(next==std::numeric_limits<uint32_t>::max()){errno=EMFILE;return failure;}
        File fp(std::fopen(path,mode));
        if(!fp)return failure;
        const auto id=next++;
        table.emplace(id,std::move(fp));
        return id;
    }
    static std::FILE* get(std::map<uint32_t, File>& table,uint32_t id) {
        const auto it=table.find(id);
        if(it==table.end()){errno=EBADF;return nullptr;}
        return it->second.get();
    }
    static int close(std::map<uint32_t, File>& table,uint32_t id) {
        const auto it=table.find(id);
        if(it==table.end()){errno=EBADF;return -1;}
        auto* fp=it->second.release();table.erase(it);
        return std::fclose(fp);
    }
public:
    uint32_t open(const char* path){return insert(descriptors,next_fd,path,"rb",UINT32_MAX);}
    uint32_t fopen(const char* path,const char* mode){return insert(streams,next_stream,path,mode,0);}
    std::FILE* descriptor(uint32_t id){return get(descriptors,id);}
    std::FILE* stream(uint32_t id){return get(streams,id);}
    int close(uint32_t id){return close(descriptors,id);}
    int fclose(uint32_t id){return close(streams,id);}
    int stat(uint32_t id,struct stat& out) {
        auto* fp=descriptor(id);
        return fp?::fstat(::fileno(fp),&out):-1;
    }

};
}
