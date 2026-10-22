#pragma once
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <map>
#include <memory>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

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
    uint32_t open(const char* path,uint32_t flags=0,uint32_t mode=0666){
        // Bionic ARM flags are guest ABI values, not host O_* constants.
        constexpr uint32_t accepted=3|64|128|512|1024|32768|524288;
        if((flags&~accepted)||(flags&3)==3){errno=EINVAL;return UINT32_MAX;}
        if(next_fd==UINT32_MAX){errno=EMFILE;return UINT32_MAX;}
        int host=(flags&3)==1?O_WRONLY:(flags&3)==2?O_RDWR:O_RDONLY;
        if(flags&64)host|=O_CREAT;
        if(flags&128)host|=O_EXCL;
        if(flags&512)host|=O_TRUNC;
        if(flags&1024)host|=O_APPEND;
        const int fd=::open(path,host,mode&0777);
        if(fd<0)return UINT32_MAX;
        const char* stream_mode=(flags&3)==2?((flags&1024)?"a+b":"r+b"):(flags&3)==1?((flags&1024)?"ab":"wb"):"rb";
        File fp(::fdopen(fd,stream_mode));
        if(!fp){const int error=errno;::close(fd);errno=error;return UINT32_MAX;}
        std::setvbuf(fp.get(),nullptr,_IONBF,0);
        const auto token=next_fd++;descriptors.emplace(token,std::move(fp));return token;
    }
    uint32_t fopen(const char* path,const char* mode){return insert(streams,next_stream,path,mode,0);}
    std::FILE* descriptor(uint32_t id){return get(descriptors,id);}
    std::FILE* stream(uint32_t id){return get(streams,id);}
    int close(uint32_t id){return close(descriptors,id);}
    int fclose(uint32_t id){return close(streams,id);}
    int flush(uint32_t id) {
        if(id){auto* fp=stream(id);return fp?std::fflush(fp):-1;}
        int result=0;
        for(auto& file:streams)if(std::fflush(file.second.get()))result=-1;
        for(auto& file:descriptors)if(std::fflush(file.second.get()))result=-1;
        return result;
    }
    int stat(uint32_t id,struct stat& out) {
        auto* fp=descriptor(id);
        return fp?::fstat(::fileno(fp),&out):-1;
    }

};
}
