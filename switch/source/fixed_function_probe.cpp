#include "platform_probe.hpp"
#include <SDL2/SDL.h>
#include <GLES/gl.h>
#include <cstring>
#include <string>
#include "game_gl_imports.hpp"

std::string fixed_function_probe() {
    std::string log="Game GLES1.1 compatibility probe (no original game code)\n";
    if (SDL_Init(SDL_INIT_VIDEO)<0) { log+=SDL_GetError(); SDL_Quit(); return log; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
    SDL_Window* w=SDL_CreateWindow("Dead Space GLES1 probe",0,0,1280,720,SDL_WINDOW_OPENGL);
    SDL_GLContext context=w?SDL_GL_CreateContext(w):nullptr;
    if (!context) {
        log+="GLES1 context FAILED: "+std::string(SDL_GetError())+"\n";
        if (w) SDL_DestroyWindow(w);
        SDL_Quit(); return log;
    }
    // Resolve against this context. A non-null pointer is not proof of extension support.
    unsigned missing=0;
    for (const auto* name:game_gl_imports) {
        if (!SDL_GL_GetProcAddress(name)) { ++missing; log+="Missing GL entry: "+std::string(name)+"\n"; }
    }
    log+="Unresolved GLES names: "+std::to_string(missing)+"\n";
    auto getString=reinterpret_cast<const GLubyte*(*)(GLenum)>(SDL_GL_GetProcAddress("glGetString"));
    const auto* ext=getString?reinterpret_cast<const char*>(getString(GL_EXTENSIONS)):nullptr;
    log+=(ext && std::strstr(ext,"GL_OES_matrix_palette"))?
        "GL_OES_matrix_palette advertised (skinning still untested)\n":
        "GL_OES_matrix_palette NOT advertised; skinning compatibility unresolved\n";
    auto matrixMode=reinterpret_cast<void(*)(GLenum)>(SDL_GL_GetProcAddress("glMatrixMode"));
    auto identity=reinterpret_cast<void(*)()>(SDL_GL_GetProcAddress("glLoadIdentity"));
    auto vertex=reinterpret_cast<void(*)(GLint,GLenum,GLsizei,const void*)>(SDL_GL_GetProcAddress("glVertexPointer"));
    auto enable=reinterpret_cast<void(*)(GLenum)>(SDL_GL_GetProcAddress("glEnableClientState"));
    auto color=reinterpret_cast<void(*)(GLfloat,GLfloat,GLfloat,GLfloat)>(SDL_GL_GetProcAddress("glColor4f"));
    auto clearColor=reinterpret_cast<void(*)(GLfloat,GLfloat,GLfloat,GLfloat)>(SDL_GL_GetProcAddress("glClearColor"));
    auto clear=reinterpret_cast<void(*)(GLbitfield)>(SDL_GL_GetProcAddress("glClear"));
    auto draw=reinterpret_cast<void(*)(GLenum,GLint,GLsizei)>(SDL_GL_GetProcAddress("glDrawArrays"));
    auto read=reinterpret_cast<void(*)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*)>(SDL_GL_GetProcAddress("glReadPixels"));
    auto viewport=reinterpret_cast<void(*)(GLint,GLint,GLsizei,GLsizei)>(SDL_GL_GetProcAddress("glViewport"));
    auto error=reinterpret_cast<GLenum(*)()>(SDL_GL_GetProcAddress("glGetError"));
    if (matrixMode && identity && vertex && enable && color && clearColor && clear && draw && read && viewport && error) {
        matrixMode(GL_PROJECTION); identity(); matrixMode(GL_MODELVIEW); identity();
        const GLfloat points[]={-0.8f,-0.8f,0.8f,-0.8f,0,0.8f};
        enable(GL_VERTEX_ARRAY); vertex(2,GL_FLOAT,0,points); color(0.2f,0.8f,1,1);
        int width=0,height=0; SDL_GL_GetDrawableSize(w,&width,&height); viewport(0,0,width,height);
        clearColor(0,0,0,1); clear(GL_COLOR_BUFFER_BIT); draw(GL_TRIANGLES,0,3);
        GLubyte pixel[4]{}; read(width/2,height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        const auto err=error();
        const bool ok=err==GL_NO_ERROR && pixel[0]>=48 && pixel[0]<=54 && pixel[1]>=200 && pixel[1]<=208 && pixel[2]>=250;
        log+=ok?"Fixed-function triangle readback: OK\n":"Fixed-function triangle readback: FAILED\n";
        SDL_GL_SwapWindow(w);
        const auto start=SDL_GetTicks();
        while (SDL_GetTicks()-start<1500) { SDL_Event e; while(SDL_PollEvent(&e)) {} SDL_Delay(10); }
    } else log+="Required fixed-function entry points missing\n";
    SDL_GL_DeleteContext(context); SDL_DestroyWindow(w); SDL_Quit();
    return log;
}
