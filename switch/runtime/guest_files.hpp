#pragma once
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <map>
#include <sys/stat.h>

namespace ds {
// ARM32 values are opaque tokens. A host FILE* must never cross the ABI.
// Owned streams are closed even when lifecycle execution aborts.
class GuestFiles {
    std::map<uint32_t,std::FILE*> streams;
    std::map<uint32_t,uint32_t> descriptors;
    uint32_t next_stream=0x71000000, next_fd=3;
    int error_=0;
    std::FILE* lookup(uint32_t token) {
        const auto it=streams.find(token);
        if(it==streams.end()){error_=EBADF;return nullptr;}
        return it->second;
    }
public:
    GuestFiles()=default;
    GuestFiles(const GuestFiles&)=delete;
    GuestFiles& operator=(const GuestFiles&)=delete;
    ~GuestFiles(){for(const auto& item:streams)std::fclose(item.second);}
    int error() const {return error_;}
    uint32_t open_stream(const char* path,const char* mode) {
        error_=0;
        if(next_stream>0x7ffffffb){error_=EMFILE;return 0;}
        auto* file=std::fopen(path,mode);
        if(!file){error_=errno;return 0;}
        const auto token=next_stream;next_stream+=4;
        streams.emplace(token,file);return token;
    }
    uint32_t open_fd(const char* path,const char* mode) {
        if(next_fd>=0x7fffffff){error_=EMFILE;return 0xffffffff;}
        const auto token=open_stream(path,mode);
        if(!token)return 0xffffffff;
        const auto fd=next_fd++;descriptors.emplace(fd,token);return fd;
    }
    uint32_t stream_for_fd(uint32_t fd) {
        error_=0;const auto it=descriptors.find(fd);
        if(it==descriptors.end()){error_=EBADF;return 0;}
        return it->second;
    }
    int close_stream(uint32_t token) {
        error_=0;auto* file=lookup(token);if(!file)return -1;
        streams.erase(token);
        for(auto it=descriptors.begin();it!=descriptors.end();) {
            if(it->second==token)it=descriptors.erase(it);else ++it;
        }
        const int rc=std::fclose(file);if(rc)error_=errno;return rc;
    }
    int close_fd(uint32_t fd) {const auto token=stream_for_fd(fd);return token?close_stream(token):-1;}
    size_t read(uint32_t token,void* out,size_t bytes) {
        error_=0;auto* file=lookup(token);if(!file)return 0;
        const auto got=std::fread(out,1,bytes,file);if(std::ferror(file))error_=errno?errno:EIO;return got;
    }
    size_t write(uint32_t token,const void* input,size_t bytes) {
        error_=0;auto* file=lookup(token);if(!file)return 0;
        const auto sent=std::fwrite(input,1,bytes,file);if(sent!=bytes)error_=errno?errno:EIO;return sent;
    }
    int seek(uint32_t token,int64_t offset,int whence) {
        error_=0;auto* file=lookup(token);if(!file)return -1;
        if(whence!=SEEK_SET&&whence!=SEEK_CUR&&whence!=SEEK_END){error_=EINVAL;return -1;}
#ifdef _WIN32
        const int rc=_fseeki64(file,offset,whence);
#else
        const int rc=fseeko(file,off_t(offset),whence);
#endif
        if(rc)error_=errno;return rc;
    }
    int64_t tell(uint32_t token) {
        error_=0;auto* file=lookup(token);if(!file)return -1;
#ifdef _WIN32
        const int64_t position=_ftelli64(file);
#else
        const int64_t position=ftello(file);
#endif
        if(position<0)error_=errno;return position;
    }
    int64_t size(uint32_t token) {
        error_=0;auto* file=lookup(token);if(!file)return -1;
#ifdef _WIN32
        struct _stat64 st{};const int rc=_fstat64(_fileno(file),&st);
#else
        struct stat st{};const int rc=::fstat(fileno(file),&st);
#endif
        if(rc){error_=errno;return -1;}return st.st_size;
    }
    int flush(uint32_t token) {
        error_=0;
        if(!token){int result=0;for(const auto& entry:streams)if(std::fflush(entry.second)){error_=errno;result=-1;}return result;}
        auto* file=lookup(token);if(!file)return -1;
        const int rc=std::fflush(file);if(rc)error_=errno;return rc;
    }
};
}
