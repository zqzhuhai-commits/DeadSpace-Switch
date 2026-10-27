#include "loading_overlay.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
int main(int argc,char** argv){try{
    using Pixels=std::vector<unsigned char>;
    auto render=[](uint64_t ms,uint64_t bytes,uint64_t frames){
        Pixels pixels(640*480*3,24);
        ds::draw_loading(ms,bytes,frames,[&](int x,int y,int w,int h,float r,float g,float b){
            if(x<0||y<0||w<1||h<1||x+w>640||y+h>480)throw std::runtime_error("Overlay rectangle out of bounds");
            for(int row=y;row<y+h;++row)for(int col=x;col<x+w;++col){auto at=(row*640+col)*3;pixels[at]=uint8_t(r*255);pixels[at+1]=uint8_t(g*255);pixels[at+2]=uint8_t(b*255);}
        });return pixels;
    };
    auto first=render(0,0,0),next=render(600,0,0),count=render(600,75*1024*1024,143);
    if(first==next||next==count)throw std::runtime_error("Activity/counters failed to advance");
    render(UINT64_MAX,UINT64_MAX,UINT64_MAX);
    if(argc>1){std::ofstream f(argv[1],std::ios::binary);f<<"P6\n640 480\n255\n";f.write(reinterpret_cast<const char*>(count.data()),count.size());if(!f)throw std::runtime_error("Preview failed");}
    std::cout<<"PASS: activity animation, observed counters, extreme bounds and texture-free rendering\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
