#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

namespace ds {
// Original 3x5 bitmap lettering; no external font or texture dependency.
inline uint16_t loading_glyph(char c){
    static constexpr uint16_t digits[]={31599,9362,29671,29647,23497,31183,31215,29257,31727,31695};
    static constexpr uint16_t letters[]={11245,27566,14627,27502,31143,31140,14699,23533,29847,4714,23469,18727,24557,24573,11114,27556,11131,27565,14478,29842,23407,23402,23549,23213,23186,29351};
    if(c>='0'&&c<='9')return digits[c-'0'];
    if(c>='A'&&c<='Z')return letters[c-'A'];
    if(c==':')return 1040;
    return 0;
}
// rect uses a 640x480, top-left coordinate space and an RGB color.
template<class Rect> void draw_loading(uint64_t elapsed_ms,uint64_t bytes,uint64_t frames,Rect rect){
    rect(0,0,640,68,0.015f,0.025f,0.04f);
    auto text=[&](int x,int y,const std::string& s){for(char c:s){const auto bits=loading_glyph(c);for(int row=0;row<5;++row)for(int col=0;col<3;++col)if(bits&(1u<<(14-row*3-col)))rect(x+col*2,y+row*2,2,2,.75f,.9f,.95f);x+=8;}};
    text(24,10,"STARTING GAME");
    char line[90];std::snprintf(line,sizeof(line),"READ %llu MB  FRAMES %llu  TIME %llu S",(unsigned long long)std::min<uint64_t>(999999,bytes/(1024*1024)),(unsigned long long)std::min<uint64_t>(99999999,frames),(unsigned long long)std::min<uint64_t>(999999,elapsed_ms/1000));
    text(24,26,line);
    rect(24,46,592,5,.09f,.18f,.24f);
    // A travelling segment is activity, never a fictitious completion percent.
    const int step=int((elapsed_ms/12)%1088);const int x=step<=544?step:1088-step;
    rect(24+x,46,48,5,.15f,.85f,.95f);
}
}
