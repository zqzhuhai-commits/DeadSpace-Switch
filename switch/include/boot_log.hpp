#pragma once
#include <cstdio>
#include <string>
namespace ds {
// Close each checkpoint before entering a subsystem that may never return.
// Never retain a stdio buffer until the interactive session ends.
inline bool boot_log(const char* path,const std::string& message) {
    FILE* file=std::fopen(path,"a");
    if(!file)return false;
    const bool written=std::fprintf(file,"%s\n",message.c_str())>=0;
    const bool flushed=std::fflush(file)==0;
    const bool closed=std::fclose(file)==0;
    return written&&flushed&&closed;
}
}
