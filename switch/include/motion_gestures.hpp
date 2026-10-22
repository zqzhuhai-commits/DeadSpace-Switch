#pragma once
#include "guest_input.hpp"
#include <vector>
namespace ds {
// Measured samples from v-atamanenko/deadspace-vita (MIT), already used by
// android/input_bridge.cpp. Keep them timed; a static vector is not a gesture.
struct MotionSample {float x,y,z;unsigned delay_ms;};
inline constexpr MotionSample kWeaponTilt[] = {
    {-0.998903f, -0.244644f, -1.292267f, 8},
    {-0.954221f, -0.367070f, -1.234914f, 8},
    {-1.029063f, -0.042471f, -1.256281f, 8},
    {-0.983264f, -0.268230f, -1.131455f, 8},
    {-1.049170f, -0.140187f, -1.296765f, 8},
    {-1.113959f, -0.304173f, -1.339498f, 8},
    {-1.655727f,  2.366763f, -2.261636f, 8},
    { 4.224865f, -2.941413f, -0.447724f, 8},
    { 2.926493f, -1.684568f, -1.615015f, 8},
    {-1.694824f,  1.514265f, -1.591399f, 8},
    {-2.287976f,  0.611223f, -1.149448f, 8},
    {-1.193269f, -0.221057f, -0.920038f, 8},
    {-1.024595f, -0.370441f, -1.369861f, 8},
    {-1.054755f, -0.157035f, -1.234914f, 8},
    {-1.159758f, -0.240151f, -1.291142f, 8},
    {-0.938582f, -0.179499f, -1.225918f, 8},
    {-1.046936f, -0.228920f, -1.210174f, 8},
    {-1.001137f, -0.234535f, -1.270900f, 8},
    {-1.055872f, -0.152542f, -1.367612f, 8},
    {-0.945284f, -0.057073f, -1.139327f, 8},
    {-1.002254f, -0.198594f, -1.224793f, 8},
    {-0.747567f, -0.235659f, -1.256281f, 8},
    {-0.699534f, -0.044716f, -1.474445f, 8},
    {-0.711821f,  0.117022f, -1.409221f, 8},
    {-1.023478f,  0.222602f, -1.628510f, 8},
    {-0.848101f, -0.006528f, -2.083956f, 8},
    {-0.272822f, -0.489498f, -3.042079f, 8},
    {-0.527508f, -0.292941f, -3.173653f, 8},
    {-1.081564f,  0.283254f, -3.189397f, 8},
    {-1.405508f,  0.348398f, -2.807047f, 8},
    {-1.814348f,  0.492166f, -2.401081f, 8},
    {-1.502691f,  0.095681f, -2.793552f, 8},
    {-1.847859f,  0.522491f, -3.261368f, 8},
};

inline constexpr MotionSample kZeroGJump[] = {
    {-1.761846f, -0.322144f, -1.657748f, 25},
    {-1.188801f, -0.022254f, -1.695983f, 8},
    {-0.794483f, -0.263738f, -2.931873f, 8},
    {-0.745333f, -0.150296f, -2.825040f, 8},
    {-0.873793f, -0.444571f, -1.743215f, 8},
    {-0.804536f, -0.127832f, -3.795533f, 8},
    {-1.742857f,  0.038399f, -1.233790f, 8},
    {-0.812356f,  0.224848f, -2.738449f, 8},
    { 0.863030f,  3.779731f, -9.721955f, 8},
    {-4.183607f,  3.622485f,  8.522037f, 8},
    {-6.169717f,  7.627765f, 23.052782f, 8},
    {-0.738630f,  6.358565f, 14.906132f, 8},
    { 0.151609f,  2.630712f,  9.449697f, 8},
    { 0.831056f,  3.416942f,  6.462183f, 8},
    {-2.905704f,  5.808204f, -9.787180f, 8},
    {-2.807403f,  1.275026f, -4.152019f, 8},
    {-2.541546f, -0.363701f,  0.349680f, 8},
    {-2.520322f,  0.236080f, -2.166049f, 8},
    {-1.934989f, -0.196346f, -0.763724f, 8},
    {-2.559419f, -0.453556f,  0.108265f, 8},
    {-1.904829f, -0.004282f, -1.038117f, 8},
    {-2.014299f, -0.291818f, -0.051879f, 8},
    {-2.021002f, -0.269354f, -1.045988f, 8},
    {-1.804294f, -0.230042f, -1.018999f, 8},
    {-2.016533f, -0.192978f, -0.518571f, 8},
    {-1.768549f, -0.101999f, -1.323754f, 8},
    {-1.541788f, -0.406383f, -1.025746f, 8},
};


class MotionGesture {
    const MotionSample* samples=nullptr;
    unsigned count=0,index=0;
    uint32_t next=0;
public:
    void start(bool jump,uint32_t now){
        if(samples)return; // Holding/repeated clicks must not restart a gesture.
        samples=jump?kZeroGJump:kWeaponTilt;
        count=jump?sizeof(kZeroGJump)/sizeof(*kZeroGJump):sizeof(kWeaponTilt)/sizeof(*kWeaponTilt);
        index=0;next=now;
    }
    bool active() const {return samples!=nullptr;}
    void poll(uint32_t now,std::vector<InputEvent>& out){
        // One sample per rendered frame: preserve ordering even at low FPS,
        // rather than flooding the guest with a collapsed gesture in one tick.
        if(!samples||int32_t(now-next)<0)return;
        const auto& v=samples[index++];
        out.push_back({InputEvent::acceleration,0,0,v.x,v.y,1000,v.z});
        next=now+v.delay_ms;
        if(index==count)samples=nullptr;
    }
};
}
