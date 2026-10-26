#pragma once
#include <cstdio>
namespace ds {
inline bool read_runtime_toggle(const char* path,bool fallback){
    auto* file=std::fopen(path,"r");if(!file)return fallback;
    unsigned value=0;char extra=0;
    const bool valid=std::fscanf(file,"%u %c",&value,&extra)==1&&value<=1;
    std::fclose(file);return valid?value!=0:fallback;
}
struct RuntimeSettings {
    bool base_mip_only=true;
    bool hd_enabled=true;
};
inline RuntimeSettings read_runtime_settings(){
    return {read_runtime_toggle("sdmc:/switch/deadspacemobile/mipmap-fix.cfg",true),
            read_runtime_toggle("sdmc:/switch/deadspacemobile/hd.cfg",true)};
}
}
