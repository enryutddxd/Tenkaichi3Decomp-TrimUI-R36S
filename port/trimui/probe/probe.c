/*
 * TrimUI Smart Pro (stock firmware): what the port needs, checked on the device, written to probe.txt.
 * The system (kernel, C library, processor, memory), SDL2's video and audio, an OpenGL ES 3 context through SDL (the
 * version, the functions the port's GL layer calls, a shader with gl_VertexID), memory mapped below 4 GB (the
 * game's 32-bit pointers in a 64-bit program), and whether 32-bit ARM programs run. Shows colours for 3 seconds.
 */
#define _GNU_SOURCE
#include <SDL2/SDL.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <gnu/libc-version.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <stdint.h>

typedef unsigned GLenum; typedef unsigned GLuint; typedef int GLint; typedef int GLsizei; typedef char GLchar; typedef unsigned char GLubyte;
typedef float GLfloat; typedef unsigned GLbitfield;
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#define GL_EXTENSIONS 0x1F03
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_MAX_VERTEX_UNIFORM_VECTORS 0x8DFB
#define GL_MAX_TEXTURE_SIZE 0x0D33

static FILE *out;
static void say(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt); vfprintf(out, fmt, ap); va_end(ap);
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fflush(out);
}
static void file(const char *path, int lines) {
    FILE *f = fopen(path, "r");
    char b[512];
    say("--- %s\n", path);
    if (f == NULL) { say("(none)\n"); return; }
    while (lines-- > 0 && fgets(b, sizeof(b), f) != NULL) { say("%s", b); }
    fclose(f);
}
static void *glib;
static void *gp(const char *n) {
    void *p = SDL_GL_GetProcAddress(n);
    if (p == NULL && glib != NULL) { p = dlsym(glib, n); }
    return p;
}

int main(int argc, char **argv) {
    struct utsname u;
    SDL_Window *w;
    SDL_GLContext c;
    int i;
    (void)argc; (void)argv;
    out = fopen("probe.txt", "w");
    if (out == NULL) { out = stderr; }
    uname(&u);
    say("BT3 probe for the TrimUI Smart Pro\nkernel: %s %s %s\nC library: glibc %s\npointer size %d bytes\n", u.sysname, u.release, u.machine,
        gnu_get_libc_version(), (int)sizeof(void *));
    file("/proc/cpuinfo", 40);
    file("/proc/meminfo", 3);
    file("/etc/os-release", 10);
    file("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", 1);
    file("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", 1);
    file("/sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies", 1);
    file("/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq", 1);
    say("LD_LIBRARY_PATH=%s\n", getenv("LD_LIBRARY_PATH") ? getenv("LD_LIBRARY_PATH") : "");

    /* memory below 4 GB at the addresses the 64-bit game would use */
    {
        static const uintptr_t at[4] = {0x40000000u, 0x60000000u, 0x80000000u, 0xC0000000u};
        for (i = 0; i < 4; i++) {
            void *p = mmap((void *)at[i], 64u << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
            say("map 64 MB at %p: %s\n", (void *)at[i], p == (void *)at[i] ? "ok" : p == MAP_FAILED ? "failed" : "elsewhere");
            if (p != MAP_FAILED) { memset(p, 1, 4096); munmap(p, 64u << 20); }
        }
        void *m = malloc(1 << 20);
        say("malloc 1 MB at %p, sbrk(0) %p, main's stack near %p\n", m, sbrk(0), (void *)&m);
        free(m);
    }
    /* 32-bit ARM programs */
    say("32-bit ARM program (armhf_test, static): ");
    fflush(out);
    {
        int r = system("./armhf_test > armhf.txt 2>&1");
        say("exit %d\n", r);
        file("armhf.txt", 3);
    }

    /* SDL2 and OpenGL ES 3 */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
        say("SDL_Init: %s\n", SDL_GetError());
        SDL_Init(SDL_INIT_VIDEO);
    }
    {
        SDL_version v;
        SDL_DisplayMode dm;
        SDL_GetVersion(&v);
        say("SDL %d.%d.%d, video driver %s, audio driver %s\n", v.major, v.minor, v.patch, SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "-",
            SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "-");
        if (SDL_GetCurrentDisplayMode(0, &dm) == 0) { say("display %d x %d @ %d Hz\n", dm.w, dm.h, dm.refresh_rate); }
        for (i = 0; i < SDL_NumJoysticks(); i++) {
            say("joystick %d: %s (game controller: %s)\n", i, SDL_JoystickNameForIndex(i), SDL_IsGameController(i) ? SDL_GameControllerNameForIndex(i) : "no");
        }
    }
    glib = dlopen("libGLESv2.so", RTLD_NOW | RTLD_GLOBAL);
    if (glib == NULL) { glib = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_GLOBAL); }
    say("libGLESv2 by dlopen: %s\n", glib ? "yes" : dlerror());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    w = SDL_CreateWindow("probe", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_SHOWN);
    say("window: %s\n", w ? "made" : SDL_GetError());
    c = w ? SDL_GL_CreateContext(w) : NULL;
    say("GLES 3 context: %s\n", c ? "made" : SDL_GetError());
    if (c != NULL) {
        const GLubyte *(*getString)(GLenum) = gp("glGetString");
        void (*getIntegerv)(GLenum, GLint *) = gp("glGetIntegerv");
        GLuint (*createShader)(GLenum) = gp("glCreateShader");
        void (*shaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *) = gp("glShaderSource");
        void (*compileShader)(GLuint) = gp("glCompileShader");
        void (*getShaderiv)(GLuint, GLenum, GLint *) = gp("glGetShaderiv");
        void (*getShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *) = gp("glGetShaderInfoLog");
        void (*clearColor)(GLfloat, GLfloat, GLfloat, GLfloat) = gp("glClearColor");
        void (*clear)(GLbitfield) = gp("glClear");
        static const char *const need[] = {"glMapBufferRange", "glUnmapBuffer", "glBindVertexArray", "glGenFramebuffers", "glFramebufferTexture2D",
                                           "glBlendFuncSeparate", "glBlendEquationSeparate", "glUniform4fv", "glDrawElements", "glTexSubImage2D",
                                           "glCopyTexSubImage2D", "glBlitFramebuffer", "glReadPixels", "glGetProgramBinary"};
        GLint a = 0, b = 0;
        int dw = 0, dh = 0;
        SDL_GL_GetDrawableSize(w, &dw, &dh);
        say("drawable %d x %d, swap interval set to 1: %d\n", dw, dh, SDL_GL_SetSwapInterval(1));
        if (getString) {
            say("GL_VENDOR %s\nGL_RENDERER %s\nGL_VERSION %s\nGLSL %s\n", getString(GL_VENDOR), getString(GL_RENDERER), getString(GL_VERSION),
                getString(GL_SHADING_LANGUAGE_VERSION));
            say("extensions: %s\n", getString(GL_EXTENSIONS));
        }
        if (getIntegerv) {
            getIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &a);
            getIntegerv(GL_MAX_TEXTURE_SIZE, &b);
            say("vertex uniform vectors %d, texture size %d\n", a, b);
        }
        for (i = 0; i < (int)(sizeof(need) / sizeof(need[0])); i++) {
            say("%s: SDL %s, dlsym %s\n", need[i], SDL_GL_GetProcAddress(need[i]) ? "yes" : "no", glib && dlsym(glib, need[i]) ? "yes" : "no");
        }
        if (createShader && shaderSource && compileShader && getShaderiv) {
            static const char *vs = "#version 300 es\nuniform vec4 U[384];\nin vec4 aPos;\nvoid main() { int k = int(float(gl_VertexID)); gl_Position = aPos * U[k % 384]; }\n";
            GLuint s = createShader(GL_VERTEX_SHADER);
            GLint ok = 0;
            char log[300] = "";
            shaderSource(s, 1, &vs, NULL);
            compileShader(s);
            getShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (!ok && getShaderInfoLog) { getShaderInfoLog(s, sizeof(log), NULL, log); }
            say("shader #version 300 es with gl_VertexID and U[384]: %s %s\n", ok ? "compiled" : "NOT compiled", log);
        }
        if (clearColor && clear) {
            static const float col[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
            for (i = 0; i < 90; i++) {
                clearColor(col[i / 30][0], col[i / 30][1], col[i / 30][2], 1);
                clear(GL_COLOR_BUFFER_BIT);
                SDL_GL_SwapWindow(w);
            }
            say("90 pictures shown (red, green, blue)\n");
        }
    }
    say("done\n");
    fclose(out);
    SDL_Quit();
    return 0;
}
