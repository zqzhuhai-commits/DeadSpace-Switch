#pragma once
#include <cstdio>
namespace ds {
inline unsigned read_render_test_mode(){
    auto* file=std::fopen("sdmc:/switch/deadspacemobile/render-test.cfg","r");
    if(!file)return 0;
    unsigned value=0;char extra=0;
    const bool valid=std::fscanf(file,"%u %c",&value,&extra)==1&&value<=3;
    std::fclose(file);return valid?value:0;
}
inline const char* render_test_name(unsigned mode){
    return mode==1?"A: omit ALL blended draws (visual effects/UI may disappear)":
        mode==2?"B: synchronize EVERY draw (may be very slow)":
        mode==3?"C: base mip only (texture detail may change)":"BASELINE: original drawing";
}
}
