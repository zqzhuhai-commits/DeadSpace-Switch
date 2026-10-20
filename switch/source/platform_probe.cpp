#include "platform_probe.hpp"
#include <SDL2/SDL.h>
#include <GLES2/gl2.h>
#include "PVRTDecompress.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

static GLuint shader(GLenum type,const char* source,std::string& log) {
    const GLuint s=glCreateShader(type);
    glShaderSource(s,1,&source,nullptr); glCompileShader(s);
    GLint ok=0; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if (!ok) { char b[512]{}; glGetShaderInfoLog(s,sizeof(b),nullptr,b); log+=b; glDeleteShader(s); return 0; }
    return s;
}
std::string platform_probe() {
    std::string log="Standalone platform test; game engine not loaded\n";
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO)<0) { log+=SDL_GetError(); SDL_Quit(); return log; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
    SDL_Window* w=SDL_CreateWindow("Dead Space Switch platform probe",0,0,1280,720,SDL_WINDOW_OPENGL|SDL_WINDOW_SHOWN);
    if (!w) { log+=SDL_GetError(); SDL_Quit(); return log; }
    SDL_GLContext context=SDL_GL_CreateContext(w);
    if (!context) { log+=SDL_GetError(); SDL_DestroyWindow(w); SDL_Quit(); return log; }
    SDL_GL_SetSwapInterval(1);
    const auto* renderer=glGetString(GL_RENDERER);
    log+="GL renderer: "+std::string(renderer?reinterpret_cast<const char*>(renderer):"unknown")+"\n";
    const GLuint vs=shader(GL_VERTEX_SHADER,"attribute vec2 p; varying vec2 uv; void main(){uv=(p+1.0)*0.5;gl_Position=vec4(p,0.0,1.0);}",log);
    const GLuint fs=shader(GL_FRAGMENT_SHADER,"precision mediump float; varying vec2 uv; uniform sampler2D image; void main(){gl_FragColor=texture2D(image,uv)*vec4(0.2,0.8,1.0,1.0);}",log);
    GLuint program=glCreateProgram();
    glAttachShader(program,vs); glAttachShader(program,fs); glBindAttribLocation(program,0,"p"); glLinkProgram(program);
    GLint linked=0; glGetProgramiv(program,GL_LINK_STATUS,&linked);
    log+=linked?"GLES2 shaders: linked\n":"GLES2 shaders: FAILED\n";
    const float vertices[]={-0.8f,-0.8f,0.8f,-0.8f,0.0f,0.8f};
    GLuint vbo=0,texture=0;
    glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo); glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,nullptr); glEnableVertexAttribArray(0);
    // A known solid white PVRTC1 4bpp fixture; exercises upstream's real decoder.
    const std::array<uint32_t,8> compressed={0,0xffffffff,0,0xffffffff,0,0xffffffff,0,0xffffffff};
    std::array<uint8_t,8*8*4> rgba{};
    pvr::PVRTDecompressPVRTC(compressed.data(),0,8,8,rgba.data());
    bool white=true;
    for (auto byte:rgba) if (byte!=255) white=false;
    log+=white?"PVRTC decoder fixture: OK\n":"PVRTC decoder fixture: FAILED\n";
    glGenTextures(1,&texture); glBindTexture(GL_TEXTURE_2D,texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
    glUseProgram(program); glUniform1i(glGetUniformLocation(program,"image"),0);
    SDL_AudioSpec wanted{},obtained{};
    wanted.freq=48000; wanted.format=AUDIO_S16SYS; wanted.channels=2; wanted.samples=1024;
    const SDL_AudioDeviceID audio=SDL_OpenAudioDevice(nullptr,0,&wanted,&obtained,0);
    if (audio) {
        // Half-second, quiet 440Hz stereo tone. Opening/queuing != audible proof.
        std::array<int16_t,48000> pcm{};
        for (unsigned i=0;i<24000;++i) pcm[i*2]=pcm[i*2+1]=int16_t(1200*std::sin(6.28318530718*440*i/48000));
        if (SDL_QueueAudio(audio,pcm.data(),sizeof(pcm))==0) {
            SDL_PauseAudioDevice(audio,0); log+="Audio opened and tone queued; confirm by listening\n";
        } else log+="Audio queue FAILED: "+std::string(SDL_GetError())+"\n";
    } else log+="Audio open FAILED: "+std::string(SDL_GetError())+"\n";
    const uint32_t start=SDL_GetTicks();
    unsigned frames=0;
    bool quit=false,readback=false;
    while (!quit && SDL_GetTicks()-start<3000) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) if (e.type==SDL_QUIT) quit=true;
        int width=0,height=0; SDL_GL_GetDrawableSize(w,&width,&height);
        glViewport(0,0,width,height); glClearColor(0.01f,0.03f,0.05f,1); glClear(GL_COLOR_BUFFER_BIT);
        if (linked) glDrawArrays(GL_TRIANGLES,0,3);
        if (frames==0) {
            uint8_t pixel[4]{}; glReadPixels(width/2,height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
            readback=linked && pixel[0]>=48 && pixel[0]<=54 && pixel[1]>=200 && pixel[1]<=208 && pixel[2]>=250;
            log+=readback?"Textured triangle framebuffer readback: OK\n":"Textured triangle framebuffer readback: FAILED\n";
        }
        SDL_GL_SwapWindow(w); ++frames;
    }
    const auto err=glGetError();
    char summary[120]; std::snprintf(summary,sizeof(summary),"Probe frames=%u GL error=0x%x (not game FPS)\n",frames,unsigned(err)); log+=summary;
    if (audio) { log+=SDL_GetQueuedAudioSize(audio)==0?"Audio queue drained (speaker unverified)\n":"Audio queue still pending\n"; SDL_CloseAudioDevice(audio); }
    glDeleteTextures(1,&texture); glDeleteBuffers(1,&vbo); glDeleteProgram(program);
    glDeleteShader(vs); glDeleteShader(fs);
    SDL_GL_DeleteContext(context); SDL_DestroyWindow(w); SDL_Quit();
    return log;
}
