#pragma once
#include <algorithm>
#include <cmath>
#include <utility>
namespace ds {
// Gravity-projected motion aiming follows hazevauks/deadspace_nx (MIT).
struct GyroInput {
    float up[3]={0,0,1};
    std::pair<float,float> update(float ax,float ay,float az,float wx,float wy,float wz,float dt){
        const float length=std::sqrt(ax*ax+ay*ay+az*az);
        if(length>.7f&&length<1.3f){up[0]+=(-ax/length-up[0])*.1f;up[1]+=(-ay/length-up[1])*.1f;up[2]+=(-az/length-up[2])*.1f;}
        if(std::fabs(wx)+std::fabs(wy)+std::fabs(wz)<.004f)return {0,0};
        const float scale=4320*std::clamp(dt,0.0f,.05f);
        return {-(wx*up[0]+wy*up[1]+wz*up[2])*scale,-wx*scale};
    }
};
}
