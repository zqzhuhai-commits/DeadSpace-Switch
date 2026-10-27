#pragma once
#include "app_paths.hpp"
#include <cstdio>
#include <cstdarg>
#include <sys/stat.h>
namespace ds {
inline constexpr const char* startup_file="sdmc:/switch/deadspacemobile/startup-latest.log";
inline void startup_log(const char* format,...) {
    if(auto* file=std::fopen(startup_file,"a")){
        va_list args;va_start(args,format);std::vfprintf(file,format,args);va_end(args);
        std::fputc('\n',file);std::fclose(file);
    }
}
inline void startup_begin(const char* version){
    mkdir("sdmc:/switch",0777);mkdir(paths::app_directory,0777);
    if(auto* file=std::fopen(startup_file,"w")){
        std::fprintf(file,"BOOT: libnx services ready; C++ constructors pending; version=%s\n",version);
        std::fclose(file);
    }
}
}
