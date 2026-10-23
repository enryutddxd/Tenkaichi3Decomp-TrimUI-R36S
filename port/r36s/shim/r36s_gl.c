/*
 * R36S: vitaGL as port/vita/vita_vgl.c uses it, on OpenGL ES 3 through SDL2 (the Mali GPU of the RK3326).
 *
 * vita_vgl.c is the Vita's renderer, written for vitaGL: OpenGL calls plus a few of vitaGL's own and a little of the
 * Vita's GPU library. Most of its calls are OpenGL ES 3's as they are; what differs:
 *
 *  - The display. vitaGL draws "framebuffer 0" into three buffers in memory, row 0 at the top, and the renderer
 *    reads them as textures (the outline, the glow, the depth tint: vglSetupTexture on gxm_color_surfaces_addr, and
 *    the depth buffer with vglTexImageDepthBuffer). Here framebuffer 0 is three framebuffer objects of our own with a
 *    shared depth / stencil texture; while one of them is bound every vertex shader turns the picture upside down
 *    (r36_flip, added by r36s_cg.c) and the viewport and scissor are mirrored, so that the textures hold the display
 *    the way vitaGL's memory does. vglSwapBuffers puts the finished buffer on the screen (turned the right way up).
 *  - Texture names are the renderer's own numbers (a texture can stand for a display buffer or the depth buffer).
 *  - Shaders are Cg (GL_CG_*_SHADER_EXT), translated to GLSL ES by r36s_cg.c.
 *  - A buffer mapped with glMapBuffer stays mapped and is written by the renderer whenever it likes (vitaGL's
 *    memory is the GPU's). Here the map is a copy in main memory; vglR36Dirty marks what was written, and it goes to
 *    the GPU before the next draw.
 *  - The fixed-function pipeline (the films' pictures and a fallback): a shader of our own.
 *  - The renderer's extensions (vglFastDraw: many draws with their own uniforms; vglSplitScene, ...).
 *
 * The GL context belongs to one thread at a time. The renderer's drawing thread holds it; another thread that calls
 * GL (the text screen of an error, say) takes it for that call and gives it back (gl_enter / gl_leave), the drawing
 * thread letting it go at its next GL call or wait.
 */
#include <SDL2/SDL.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "vitaGL.h"

extern void Vita_Log(const char *fmt, ...);
extern char *R36_CgToGlsl(const char *src, int len, int vertex, char *err, int errlen);
extern void R36_LogLine(const char *fmt, ...);

/* ---- the real entry points (from SDL_GL_GetProcAddress) ---- */
#define R36_REAL_LIST \
    X(void, glActiveTexture, (GLenum)) X(void, glAttachShader, (GLuint, GLuint)) X(void, glBindAttribLocation, (GLuint, GLuint, const GLchar *)) \
    X(void, glBindBuffer, (GLenum, GLuint)) X(void, glBindFramebuffer, (GLenum, GLuint)) X(void, glBindTexture, (GLenum, GLuint)) \
    X(void, glBlendEquation, (GLenum)) X(void, glBlendEquationSeparate, (GLenum, GLenum)) X(void, glBlendFunc, (GLenum, GLenum)) \
    X(void, glBlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum)) X(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum)) \
    X(void, glBufferSubData, (GLenum, GLintptr, GLsizeiptr, const void *)) X(GLenum, glCheckFramebufferStatus, (GLenum)) \
    X(void, glClear, (GLbitfield)) X(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) X(void, glClearDepthf, (GLfloat)) \
    X(void, glClearStencil, (GLint)) X(void, glColorMask, (GLboolean, GLboolean, GLboolean, GLboolean)) X(void, glCompileShader, (GLuint)) \
    X(GLuint, glCreateProgram, (void)) X(GLuint, glCreateShader, (GLenum)) X(void, glDeleteBuffers, (GLsizei, const GLuint *)) \
    X(void, glDeleteFramebuffers, (GLsizei, const GLuint *)) X(void, glDeleteTextures, (GLsizei, const GLuint *)) X(void, glDepthFunc, (GLenum)) \
    X(void, glDepthMask, (GLboolean)) X(void, glDisable, (GLenum)) X(void, glDisableVertexAttribArray, (GLuint)) \
    X(void, glDrawArrays, (GLenum, GLint, GLsizei)) X(void, glDrawElements, (GLenum, GLsizei, GLenum, const void *)) X(void, glEnable, (GLenum)) \
    X(void, glEnableVertexAttribArray, (GLuint)) X(void, glFinish, (void)) X(void, glFlush, (void)) \
    X(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) X(void, glGenBuffers, (GLsizei, GLuint *)) \
    X(void, glGenFramebuffers, (GLsizei, GLuint *)) X(void, glGenTextures, (GLsizei, GLuint *)) X(GLenum, glGetError, (void)) \
    X(void, glGetIntegerv, (GLenum, GLint *)) X(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(void, glGetProgramiv, (GLuint, GLenum, GLint *)) X(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(void, glGetShaderiv, (GLuint, GLenum, GLint *)) X(const GLubyte *, glGetString, (GLenum)) X(GLint, glGetUniformLocation, (GLuint, const GLchar *)) \
    X(void, glGetActiveUniform, (GLuint, GLuint, GLsizei, GLsizei *, GLint *, GLenum *, GLchar *)) X(void, glLinkProgram, (GLuint)) \
    X(void, glPixelStorei, (GLenum, GLint)) X(void, glScissor, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) X(void, glStencilFunc, (GLenum, GLint, GLuint)) \
    X(void, glStencilMask, (GLuint)) X(void, glStencilOp, (GLenum, GLenum, GLenum)) \
    X(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
    X(void, glTexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *)) \
    X(void, glTexParameteri, (GLenum, GLenum, GLint)) X(void, glUniform1f, (GLint, GLfloat)) X(void, glUniform1i, (GLint, GLint)) \
    X(void, glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat)) X(void, glUniform4fv, (GLint, GLsizei, const GLfloat *)) \
    X(void, glUseProgram, (GLuint)) X(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
    X(void, glVertexAttrib4f, (GLuint, GLfloat, GLfloat, GLfloat, GLfloat)) X(void, glViewport, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, glBindVertexArray, (GLuint))
#define X(r, n, p) static r(*p_##n) p;
R36_REAL_LIST
#undef X

/* ---- the window and the context ---- */
static SDL_Window *sWin;
static SDL_GLContext sCtx;
static int sWinW = 640, sWinH = 480;
static int sVideoUp;
int gR36Vsync = 1;

/* ---- who holds the context ---- */
static pthread_mutex_t sCtxLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sCtxCond = PTHREAD_COND_INITIALIZER;
static int sCtxHeld;          /* a thread holds it */
static volatile int sCtxWant; /* threads waiting for it */
static __thread int tHold;    /* this thread holds it */
static __thread int tPrimary; /* this thread keeps it (the drawing thread) */
static int sPrimarySet;

static void ctx_take(void) {
    struct timespec ts;
    int timedOut = 0;
    pthread_mutex_lock(&sCtxLock);
    sCtxWant++;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 3;
    while (sCtxHeld && !timedOut) {
        timedOut = pthread_cond_timedwait(&sCtxCond, &sCtxLock, &ts) != 0;
    }
    sCtxWant--;
    if (sCtxHeld) { /* the holder did not let it go in 3 s: nothing to do but go on without it */
        pthread_mutex_unlock(&sCtxLock);
        R36_LogLine("r36 gl: the GL context was not given up by the thread holding it (3 s)\n");
        return;
    }
    sCtxHeld = 1;
    pthread_cond_broadcast(&sCtxCond);
    pthread_mutex_unlock(&sCtxLock);
    SDL_GL_MakeCurrent(sWin, sCtx);
    tHold = 1;
}
static void ctx_give(void) {
    SDL_GL_MakeCurrent(sWin, NULL);
    tHold = 0;
    pthread_mutex_lock(&sCtxLock);
    sCtxHeld = 0;
    pthread_cond_broadcast(&sCtxCond);
    pthread_mutex_unlock(&sCtxLock);
}
/* the drawing thread, at a GL call or a wait: another thread wants the context for a moment */
void R36_GlYield(void) {
    if (tHold && tPrimary && sCtxWant) {
        struct timespec ts;
        ctx_give();
        /* (until the one waiting has it: else this thread could take it straight back) */
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 50000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        pthread_mutex_lock(&sCtxLock);
        while (!sCtxHeld && sCtxWant > 0) {
            if (pthread_cond_timedwait(&sCtxCond, &sCtxLock, &ts) != 0) {
                break;
            }
        }
        pthread_mutex_unlock(&sCtxLock);
        ctx_take();
    }
}
/* returns 1 when the context was taken for this call only */
static inline int gl_enter(void) {
    if (__builtin_expect(tHold, 1)) {
        if (__builtin_expect(sCtxWant != 0, 0)) {
            R36_GlYield();
        }
        return 0;
    }
    ctx_take();
    if (!tHold) {
        return -1;
    }
    if (tPrimary) {
        return 0;
    }
    return 1;
}
static inline void gl_leave(int f) {
    if (f == 1) {
        ctx_give();
    }
}
/* this thread keeps the context from now on (vglInitExtended: the drawing thread) */
static void gl_make_primary(void) {
    tPrimary = 1;
    sPrimarySet = 1;
}
#define ENTER() int gl_f_ = gl_enter(); if (gl_f_ < 0) { return; }
#define ENTER_R(v) int gl_f_ = gl_enter(); if (gl_f_ < 0) { return v; }
#define LEAVE() gl_leave(gl_f_)

/* ---- state the shim keeps track of ---- */
#define NDISP 3
typedef struct VTex {
    GLuint real;
    int alias;              /* 0 its own, 1..3 display buffer n-1, 4 the depth buffer */
    int used;
    SceGxmTexture gxm;      /* (vglGetGxmTexture: a stand-in nothing reads) */
} VTex;
static VTex *sTex;
static int sTexCap, sTexFree = -1;
static GLuint sBound[4];    /* the renderer's texture per unit */
static int sUnit;

static GLuint sDispTex[NDISP], sDispFbo[NDISP], sDepthTex;
static int sDispW, sDispH, sNewW, sNewH;
static int sOnDisp = 1;     /* framebuffer 0 (the display) is bound */
static GLuint sFboBound;    /* the renderer's framebuffer object, or 0 */
static GLint sVp[4], sSc[4];
static int sScOn;

void *gxm_color_surfaces_addr[NDISP];
static uint8_t *sDispMem;   /* (what those addresses point at: zeros; the renderer only compares them, or reads them in tests) */
unsigned int gxm_back_buffer_index, gxm_front_buffer_index;
SceGxmContext *gxm_context;
int vgl_bt3_split_on;
unsigned vgl_bt3_splits;
uint32_t vgl_flip_hist[5];
uint64_t vgl_dbg_time[8];
unsigned vgl_bt3_frag_patches, vgl_bt3_frag_hits, vgl_bt3_frag_us, vgl_bt3_layout_hits, vgl_bt3_layout_misses; /* (vitaGL's figures for the log) */
uint32_t vsync_interval;
static void (*sDisplayCb)(void *);

/* programs: where their r36_flip and U are */
typedef struct Prog { GLint flip, u; float flipVal; uint32_t usize; } Prog;
static Prog *sProg;
static int sProgCap;
static GLuint sCurProg;
static Prog *prog_of(GLuint p) {
    if ((int)p >= sProgCap) {
        int n = (int)p * 2 + 64;
        sProg = (Prog *)realloc(sProg, sizeof(Prog) * (size_t)n);
        memset(sProg + sProgCap, 0, sizeof(Prog) * (size_t)(n - sProgCap));
        sProgCap = n;
    }
    return &sProg[p];
}
/* shaders: which are Cg, and their GLSL (for the log when one does not compile) */
typedef struct Shd { int cg, vertex; char *glsl; } Shd;
static Shd *sShd;
static int sShdCap;
static Shd *shd_of(GLuint s) {
    if ((int)s >= sShdCap) {
        int n = (int)s * 2 + 64;
        sShd = (Shd *)realloc(sShd, sizeof(Shd) * (size_t)n);
        memset(sShd + sShdCap, 0, sizeof(Shd) * (size_t)(n - sShdCap));
        sShdCap = n;
    }
    return &sShd[s];
}

/* buffers: their size, and the copy a map gives */
typedef struct MapRange { uint32_t lo, hi; } MapRange;
typedef struct Buf { GLuint id; uint32_t size; uint8_t *shadow; MapRange r[32]; int nr; } Buf;
static Buf sBufs[16];
static int sNBufs;
static GLuint sArrayBuf, sElemBuf;
static Buf *buf_find(GLuint id, int make) {
    int i;
    for (i = 0; i < sNBufs; i++) {
        if (sBufs[i].id == id) {
            return &sBufs[i];
        }
    }
    if (!make || sNBufs == 16) {
        return NULL;
    }
    memset(&sBufs[sNBufs], 0, sizeof(Buf));
    sBufs[sNBufs].id = id;
    return &sBufs[sNBufs++];
}
static volatile int sDirty;

/* the vertex attributes 0..3 as the renderer set them (the fixed-function draws and the present use them too) */
typedef struct Attr { int on, set; GLint size; GLenum type; GLboolean norm; GLsizei stride; const void *ptr; GLuint buf; } Attr;
static Attr sAttr[4];

/* the fixed-function pipeline's state */
static struct {
    int tex2d, atest, va, ca, ta;
    GLenum afunc;
    float aref;
    float color[4];
    GLenum env;
    Attr v, c, t;
} sFF = {0, 0, 0, 0, 0, GL_ALWAYS, 0.0f, {1, 1, 1, 1}, GL_MODULATE};
static GLuint sFFProg, sPresentProg;
static GLint sFFU[6], sPresentU[3];

/* ---- loading ---- */
static void *proc(const char *name) {
    void *p = SDL_GL_GetProcAddress(name);
    if (p == NULL) {
        R36_LogLine("r36 gl: %s not found\n", name);
    }
    return p;
}

static GLuint mk_prog(const char *vs, const char *fs) {
    GLuint v = p_glCreateShader(GL_VERTEX_SHADER), f = p_glCreateShader(GL_FRAGMENT_SHADER), p = p_glCreateProgram();
    GLint ok = 0;
    char log[600];
    p_glShaderSource(v, 1, &vs, NULL);
    p_glCompileShader(v);
    p_glGetShaderiv(v, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        p_glGetShaderInfoLog(v, sizeof(log), NULL, log);
        R36_LogLine("r36 gl: own vertex shader: %s\n", log);
    }
    p_glShaderSource(f, 1, &fs, NULL);
    p_glCompileShader(f);
    p_glGetShaderiv(f, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        p_glGetShaderInfoLog(f, sizeof(log), NULL, log);
        R36_LogLine("r36 gl: own pixel shader: %s\n", log);
    }
    p_glAttachShader(p, v);
    p_glAttachShader(p, f);
    p_glBindAttribLocation(p, 0, "aPos");
    p_glBindAttribLocation(p, 1, "aCol");
    p_glBindAttribLocation(p, 2, "aTex");
    p_glLinkProgram(p);
    p_glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        p_glGetProgramInfoLog(p, sizeof(log), NULL, log);
        R36_LogLine("r36 gl: own program: %s\n", log);
    }
    return p;
}

static const char kFFVert[] =
    "#version 300 es\nprecision highp float;\n"
    "in vec4 aPos; in vec4 aCol; in vec2 aTex;\n"
    "uniform float r36_flip; uniform vec4 uColor; uniform float uHasCol;\n"
    "out vec4 vCol; out vec2 vTex;\n"
    "void main() { gl_Position = aPos; gl_Position.y *= r36_flip; vCol = uHasCol > 0.5 ? aCol : uColor; vTex = aTex; }\n";
static const char kFFFrag[] =
    "#version 300 es\nprecision highp float;\n"
    "in vec4 vCol; in vec2 vTex; out vec4 oCol;\n"
    "uniform sampler2D uTex; uniform vec4 uEnv;\n" /* x: 0 none, 1 modulate, 2 replace, 3 decal; y: alpha test (0 off, 1+ the function), z: reference */
    "void main() {\n"
    "    vec4 c = vCol;\n"
    "    if (uEnv.x > 0.5) { vec4 t = texture(uTex, vTex);\n"
    "        if (uEnv.x < 1.5) { c = c * t; } else if (uEnv.x < 2.5) { c = t; } else { c = vec4(mix(c.rgb, t.rgb, t.a), c.a); } }\n"
    "    if (uEnv.y > 0.5) { float d = c.a - uEnv.z; bool pass = true;\n"
    "        if (uEnv.y < 1.5) pass = false; else if (uEnv.y < 2.5) pass = true; else if (uEnv.y < 3.5) pass = d < 0.0;\n"
    "        else if (uEnv.y < 4.5) pass = d <= 0.0; else if (uEnv.y < 5.5) pass = d == 0.0; else if (uEnv.y < 6.5) pass = d >= 0.0;\n"
    "        else if (uEnv.y < 7.5) pass = d > 0.0; else pass = d != 0.0;\n"
    "        if (!pass) discard; }\n"
    "    oCol = c;\n"
    "}\n";
static const char kPresentVert[] =
    "#version 300 es\nprecision highp float;\n"
    "in vec4 aPos; out vec2 vTex; uniform vec4 uR;\n" /* xy: scale, zw: offset of the picture on the screen */
    "void main() { gl_Position = vec4(aPos.xy * uR.xy + uR.zw, 0.0, 1.0); vTex = vec2(aPos.x * 0.5 + 0.5, 0.5 - aPos.y * 0.5); }\n";
static const char kPresentFrag[] =
    "#version 300 es\nprecision highp float;\n"
    "in vec2 vTex; out vec4 oCol; uniform sampler2D uTex;\n"
    "void main() { oCol = vec4(texture(uTex, vTex).rgb, 1.0); }\n";

int R36_VideoInit(void) {
    SDL_DisplayMode dm;
    if (sVideoUp) {
        return 1;
    }
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        R36_LogLine("r36: SDL video: %s\n", SDL_GetError());
        return 0;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    if (SDL_GetCurrentDisplayMode(0, &dm) == 0 && dm.w > 0 && dm.h > 0) {
        sWinW = dm.w;
        sWinH = dm.h;
    }
    sWin = SDL_CreateWindow("Dragon Ball Z Budokai Tenkaichi 3", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, sWinW, sWinH,
                            SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_SHOWN);
    if (sWin == NULL) {
        R36_LogLine("r36: no window: %s\n", SDL_GetError());
        return 0;
    }
    sCtx = SDL_GL_CreateContext(sWin);
    if (sCtx == NULL) {
        R36_LogLine("r36: no OpenGL ES 3 context: %s\n", SDL_GetError());
        return 0;
    }
    SDL_ShowCursor(SDL_DISABLE);
    {
        const char *v = getenv("R36_VSYNC");
        gR36Vsync = v != NULL ? atoi(v) : 1;
        SDL_GL_SetSwapInterval(gR36Vsync);
    }
    SDL_GL_GetDrawableSize(sWin, &sWinW, &sWinH);
#define X(r, n, p) p_##n = (r(*) p)proc(#n);
    R36_REAL_LIST
#undef X
    R36_LogLine("r36: OpenGL ES: %s | %s | %s | GLSL %s; screen %d x %d\n", (const char *)p_glGetString(GL_VENDOR), (const char *)p_glGetString(GL_RENDERER),
                (const char *)p_glGetString(GL_VERSION), (const char *)p_glGetString(GL_SHADING_LANGUAGE_VERSION), sWinW, sWinH);
    {
        GLint a = 0, b = 0, c = 0;
        p_glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &a);
        p_glGetIntegerv(GL_MAX_TEXTURE_SIZE, &b);
        p_glGetIntegerv(GL_MAX_VARYING_VECTORS, &c);
        R36_LogLine("r36: vertex uniform vectors %d, texture size %d, varyings %d\n", a, b, c);
    }
    sFFProg = mk_prog(kFFVert, kFFFrag);
    sFFU[0] = p_glGetUniformLocation(sFFProg, "r36_flip");
    sFFU[1] = p_glGetUniformLocation(sFFProg, "uColor");
    sFFU[2] = p_glGetUniformLocation(sFFProg, "uHasCol");
    sFFU[3] = p_glGetUniformLocation(sFFProg, "uTex");
    sFFU[4] = p_glGetUniformLocation(sFFProg, "uEnv");
    sPresentProg = mk_prog(kPresentVert, kPresentFrag);
    sPresentU[0] = p_glGetUniformLocation(sPresentProg, "uR");
    sPresentU[1] = p_glGetUniformLocation(sPresentProg, "uTex");
    p_glClearColor(0, 0, 0, 1);
    p_glClear(GL_COLOR_BUFFER_BIT);
    SDL_GL_SwapWindow(sWin);
    sVideoUp = 1;
    tHold = 1;
    sCtxHeld = 1;
    ctx_give(); /* (taken by whichever thread draws) */
    return 1;
}
void R36_ScreenSize(int *w, int *h) {
    *w = sWinW;
    *h = sWinH;
}

/* ---- textures ---- */
static int vtex_new(void) {
    int v;
    static int next = 1;
    if (sTexFree >= 0) {
        v = sTexFree;
        sTexFree = (int)sTex[v].real;
    } else {
        if (next >= sTexCap) {
            int n = sTexCap ? sTexCap * 2 : 1024;
            sTex = (VTex *)realloc(sTex, sizeof(VTex) * (size_t)n);
            memset(sTex + sTexCap, 0, sizeof(VTex) * (size_t)(n - sTexCap));
            sTexCap = n;
        }
        v = next++;
    }
    memset(&sTex[v], 0, sizeof(VTex));
    sTex[v].used = 1;
    return v;
}
static GLuint real_tex(GLuint v) {
    if (v == 0 || (int)v >= sTexCap || !sTex[v].used) {
        return 0;
    }
    if (sTex[v].alias >= 1 && sTex[v].alias <= NDISP) {
        return sDispTex[sTex[v].alias - 1];
    }
    if (sTex[v].alias == 4) {
        return sDepthTex;
    }
    return sTex[v].real;
}
void r36_glGenTextures(GLsizei n, GLuint *t) {
    ENTER();
    for (GLsizei i = 0; i < n; i++) {
        int v = vtex_new();
        p_glGenTextures(1, &sTex[v].real);
        t[i] = (GLuint)v;
    }
    LEAVE();
}
void r36_glDeleteTextures(GLsizei n, const GLuint *t) {
    ENTER();
    for (GLsizei i = 0; i < n; i++) {
        GLuint v = t[i];
        if (v == 0 || (int)v >= sTexCap || !sTex[v].used) {
            continue;
        }
        if (sTex[v].real != 0) {
            p_glDeleteTextures(1, &sTex[v].real);
        }
        for (int u = 0; u < 4; u++) {
            if (sBound[u] == v) {
                sBound[u] = 0;
            }
        }
        sTex[v].used = 0;
        sTex[v].alias = 0;
        sTex[v].real = (GLuint)sTexFree;
        sTexFree = (int)v;
    }
    LEAVE();
}
void r36_glBindTexture(GLenum target, GLuint t) {
    ENTER();
    sBound[sUnit & 3] = t;
    p_glBindTexture(target, real_tex(t));
    LEAVE();
}
void r36_glActiveTexture(GLenum u) {
    ENTER();
    sUnit = (int)(u - GL_TEXTURE0);
    p_glActiveTexture(u);
    LEAVE();
}
void r36_glTexImage2D(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border, GLenum fmt, GLenum type, const void *px) {
    ENTER();
    GLuint v = sBound[sUnit & 3];
    if (v != 0 && (int)v < sTexCap && sTex[v].alias != 0) { /* (a display buffer's own memory: not given new contents) */
        LEAVE();
        return;
    }
    p_glTexImage2D(target, level, ifmt, w, h, border, fmt, type, px);
    LEAVE();
}
void r36_glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum type, const void *px) {
    ENTER();
    p_glTexSubImage2D(target, level, x, y, w, h, fmt, type, px);
    LEAVE();
}
void r36_glTexParameteri(GLenum target, GLenum pname, GLint param) {
    ENTER();
    GLuint v = sBound[sUnit & 3];
    if (v != 0 && (int)v < sTexCap && sTex[v].alias == 4 && (pname == GL_TEXTURE_MIN_FILTER || pname == GL_TEXTURE_MAG_FILTER)) {
        param = GL_NEAREST; /* (a depth texture is not filtered in OpenGL ES 3) */
    }
    p_glTexParameteri(target, pname, param);
    LEAVE();
}
void vglSetupTexture(GLenum target, void *data, SceGxmTextureFormat format, SceGxmTextureType type, GLsizei width, GLsizei height, GLsizei mip_count) {
    GLuint v = sBound[sUnit & 3];
    int i;
    (void)target; (void)type; (void)mip_count;
    if (v == 0 || (int)v >= sTexCap) {
        return;
    }
    for (i = 0; i < NDISP; i++) {
        if (data != NULL && data == gxm_color_surfaces_addr[i]) {
            ENTER();
            sTex[v].alias = 1 + i;
            p_glBindTexture(GL_TEXTURE_2D, sDispTex[i]);
            LEAVE();
            return;
        }
    }
    R36_LogLine("r36 gl: vglSetupTexture of other memory (format %08x, %d x %d): not supported\n", (unsigned)format, (int)width, (int)height);
}
void vglTexImageDepthBuffer(GLenum target) {
    GLuint v = sBound[sUnit & 3];
    (void)target;
    if (v != 0 && (int)v < sTexCap) {
        ENTER();
        sTex[v].alias = 4;
        p_glBindTexture(GL_TEXTURE_2D, sDepthTex);
        LEAVE();
    }
}
SceGxmTexture *vglGetGxmTexture(GLenum target) {
    GLuint v = sBound[sUnit & 3];
    (void)target;
    return v != 0 && (int)v < sTexCap ? &sTex[v].gxm : NULL;
}
void vglOverloadTexDataPointer(GLenum target, void *data) { (void)target; (void)data; }
void *vglGetTexDataPointer(GLenum target) { (void)target; return NULL; }
void vglLazyFree(void *addr) { free(addr); }
void *gpu_alloc_mapped_aligned_for_gpu(size_t alignment, size_t size) {
    void *p = NULL;
    return posix_memalign(&p, alignment < sizeof(void *) ? sizeof(void *) : alignment, size) == 0 ? p : NULL;
}
size_t vglMemFree(vglMemType type) { (void)type; return 256u << 20; }

/* ---- sceGxm's texture calls: their textures are not the ones drawn with here ---- */
int sceGxmTextureInitLinear(SceGxmTexture *t, const void *d, SceGxmTextureFormat f, unsigned int w, unsigned int h, unsigned int m) { (void)t; (void)d; (void)f; (void)w; (void)h; (void)m; return 0; }
int sceGxmTextureInitSwizzled(SceGxmTexture *t, const void *d, SceGxmTextureFormat f, unsigned int w, unsigned int h, unsigned int m) { (void)t; (void)d; (void)f; (void)w; (void)h; (void)m; return -1; }
int sceGxmTextureSetMinFilter(SceGxmTexture *t, SceGxmTextureFilter f) { (void)t; (void)f; return 0; }
int sceGxmTextureSetMagFilter(SceGxmTexture *t, SceGxmTextureFilter f) { (void)t; (void)f; return 0; }
int sceGxmTextureSetUAddrMode(SceGxmTexture *t, SceGxmTextureAddrMode m) { (void)t; (void)m; return 0; }
int sceGxmTextureSetVAddrMode(SceGxmTexture *t, SceGxmTextureAddrMode m) { (void)t; (void)m; return 0; }
SceGxmTextureFilter sceGxmTextureGetMinFilter(const SceGxmTexture *t) { (void)t; return SCE_GXM_TEXTURE_FILTER_POINT; }
SceGxmTextureFilter sceGxmTextureGetMagFilter(const SceGxmTexture *t) { (void)t; return SCE_GXM_TEXTURE_FILTER_POINT; }
SceGxmTextureAddrMode sceGxmTextureGetUAddrMode(const SceGxmTexture *t) { (void)t; return SCE_GXM_TEXTURE_ADDR_CLAMP; }
SceGxmTextureAddrMode sceGxmTextureGetVAddrMode(const SceGxmTexture *t) { (void)t; return SCE_GXM_TEXTURE_ADDR_CLAMP; }
SceGxmTextureFormat sceGxmTextureGetFormat(const SceGxmTexture *t) { (void)t; return SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR; }
void *sceGxmTextureGetData(const SceGxmTexture *t) { (void)t; return NULL; }
int sceGxmFinish(SceGxmContext *c) { (void)c; r36_glFinish(); return 0; }

/* ---- the display: three framebuffer objects and a depth / stencil texture ---- */
static void disp_alloc(int w, int h) {
    int i;
    if (sDispTex[0] == 0) {
        p_glGenTextures(NDISP, sDispTex);
        p_glGenTextures(1, &sDepthTex);
        p_glGenFramebuffers(NDISP, sDispFbo);
    }
    for (i = 0; i < NDISP; i++) {
        p_glBindTexture(GL_TEXTURE_2D, sDispTex[i]);
        p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    p_glBindTexture(GL_TEXTURE_2D, sDepthTex);
    p_glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, w, h, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    for (i = 0; i < NDISP; i++) {
        GLenum st;
        p_glBindFramebuffer(GL_FRAMEBUFFER, sDispFbo[i]);
        p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sDispTex[i], 0);
        p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, sDepthTex, 0);
        st = p_glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (st != GL_FRAMEBUFFER_COMPLETE) {
            R36_LogLine("r36 gl: display buffer %d not complete (%x)\n", i, (unsigned)st);
        }
    }
    sDispW = w;
    sDispH = h;
    R36_LogLine("r36 gl: display buffers %d x %d\n", w, h);
}
/* the renderer's viewport and scissor, as the target bound wants them (the display: mirrored) */
static void apply_vp(void) {
    if (sOnDisp) {
        p_glViewport(sVp[0], sDispH - sVp[1] - sVp[3], sVp[2], sVp[3]);
        p_glScissor(sSc[0], sDispH - sSc[1] - sSc[3], sSc[2], sSc[3]);
    } else {
        p_glViewport(sVp[0], sVp[1], sVp[2], sVp[3]);
        p_glScissor(sSc[0], sSc[1], sSc[2], sSc[3]);
    }
}
static void bind_target(void) {
    p_glBindFramebuffer(GL_FRAMEBUFFER, sOnDisp ? sDispFbo[gxm_back_buffer_index % NDISP] : sFboBound);
    apply_vp();
}
void r36_glBindFramebuffer(GLenum target, GLuint fb) {
    ENTER();
    (void)target;
    sOnDisp = fb == 0;
    sFboBound = fb;
    bind_target();
    LEAVE();
}
void r36_glFramebufferTexture2D(GLenum target, GLenum att, GLenum textarget, GLuint t, GLint level) {
    ENTER();
    p_glFramebufferTexture2D(target, att, textarget, real_tex(t), level);
    LEAVE();
}
void r36_glViewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    ENTER();
    sVp[0] = x; sVp[1] = y; sVp[2] = w; sVp[3] = h;
    p_glViewport(x, sOnDisp ? sDispH - y - h : y, w, h);
    LEAVE();
}
void r36_glScissor(GLint x, GLint y, GLsizei w, GLsizei h) {
    ENTER();
    sSc[0] = x; sSc[1] = y; sSc[2] = w; sSc[3] = h;
    p_glScissor(x, sOnDisp ? sDispH - y - h : y, w, h);
    LEAVE();
}
void r36_glClear(GLbitfield mask) {
    ENTER();
    p_glClear(mask);
    LEAVE();
}

static GLboolean sMask[4] = {1, 1, 1, 1};
void r36_glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    ENTER();
    sMask[0] = r; sMask[1] = g; sMask[2] = b; sMask[3] = a;
    p_glColorMask(r, g, b, a);
    LEAVE();
}

/* ---- capabilities: the fixed-function ones are the shim's ---- */
static int sCapBlend, sCapDepth, sCapStencil, sCapCull;
static void cap(GLenum c, int on) {
    switch (c) {
    case GL_ALPHA_TEST: sFF.atest = on; return;
    case GL_TEXTURE_2D: sFF.tex2d = on; return;
    case GL_SCISSOR_TEST: sScOn = on; break;
    case GL_BLEND: sCapBlend = on; break;
    case GL_DEPTH_TEST: sCapDepth = on; break;
    case GL_STENCIL_TEST: sCapStencil = on; break;
    case GL_CULL_FACE: sCapCull = on; break;
    default: break;
    }
    if (on) {
        p_glEnable(c);
    } else {
        p_glDisable(c);
    }
}
void r36_glEnable(GLenum c) { ENTER(); cap(c, 1); LEAVE(); }
void r36_glDisable(GLenum c) { ENTER(); cap(c, 0); LEAVE(); }

/* ---- shaders and programs ---- */
GLuint r36_glCreateShader(GLenum type) {
    ENTER_R(0);
    int cg = type == GL_CG_VERTEX_SHADER_EXT || type == GL_CG_FRAGMENT_SHADER_EXT;
    int vertex = type == GL_CG_VERTEX_SHADER_EXT || type == GL_VERTEX_SHADER;
    GLuint s = p_glCreateShader(vertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
    Shd *d = shd_of(s);
    d->cg = cg;
    d->vertex = vertex;
    LEAVE();
    return s;
}
void r36_glShaderSource(GLuint s, GLsizei count, const GLchar *const *str, const GLint *len) {
    ENTER();
    Shd *d = shd_of(s);
    if (!d->cg) {
        p_glShaderSource(s, count, str, len);
    } else {
        /* the pieces put together, then translated */
        size_t total = 0, at = 0;
        char *all, *glsl, err[200];
        for (GLsizei i = 0; i < count; i++) {
            total += len != NULL && len[i] >= 0 ? (size_t)len[i] : strlen(str[i]);
        }
        all = (char *)malloc(total + 1);
        for (GLsizei i = 0; i < count; i++) {
            size_t n = len != NULL && len[i] >= 0 ? (size_t)len[i] : strlen(str[i]);
            memcpy(all + at, str[i], n);
            at += n;
        }
        all[at] = 0;
        glsl = R36_CgToGlsl(all, (int)at, d->vertex, err, sizeof(err));
        if (glsl == NULL) {
            static char bad[300];
            const char *b = bad;
            snprintf(bad, sizeof(bad), "#version 300 es\n#error %s\n", err);
            R36_LogLine("r36 gl: a Cg shader not translated: %s\n%s\n", err, all);
            p_glShaderSource(s, 1, &b, NULL);
        } else {
            const char *g = glsl;
            p_glShaderSource(s, 1, &g, NULL);
            free(d->glsl);
            d->glsl = glsl;
        }
        free(all);
    }
    LEAVE();
}
void r36_glCompileShader(GLuint s) {
    ENTER();
    GLint ok = 0;
    p_glCompileShader(s);
    p_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1000];
        Shd *d = shd_of(s);
        log[0] = 0;
        p_glGetShaderInfoLog(s, sizeof(log), NULL, log);
        R36_LogLine("r36 gl: shader %u not compiled: %s\n---- its GLSL:\n%s\n----\n", (unsigned)s, log, d->glsl ? d->glsl : "(not Cg)");
    }
    LEAVE();
}
void r36_glShaderBinary(GLsizei count, const GLuint *shaders, GLenum fmt, const void *bin, GLsizei len) {
    (void)count; (void)shaders; (void)fmt; (void)bin; (void)len; /* (no shader binaries: vglGetShaderBinary gives none) */
}
void vglGetShaderBinary(GLuint index, GLsizei bufSize, GLsizei *length, void *binary) {
    (void)index; (void)bufSize; (void)binary;
    if (length != NULL) {
        *length = 0;
    }
}
void r36_glLinkProgram(GLuint p) {
    ENTER();
    GLint ok = 0, n = 0;
    Prog *pr;
    p_glLinkProgram(p);
    p_glGetProgramiv(p, GL_LINK_STATUS, &ok);
    pr = prog_of(p);
    memset(pr, 0, sizeof(*pr));
    if (!ok) {
        char log[600];
        log[0] = 0;
        p_glGetProgramInfoLog(p, sizeof(log), NULL, log);
        R36_LogLine("r36 gl: program %u not linked: %s\n", (unsigned)p, log);
        pr->flip = pr->u = -1;
        LEAVE();
        return;
    }
    pr->flip = p_glGetUniformLocation(p, "r36_flip");
    pr->u = p_glGetUniformLocation(p, "U");
    pr->flipVal = 0.0f; /* (set before its first draw) */
    p_glGetProgramiv(p, GL_ACTIVE_UNIFORMS, &n);
    for (GLint i = 0; i < n; i++) {
        char name[64];
        GLint size = 0;
        GLenum type = 0;
        p_glGetActiveUniform(p, (GLuint)i, sizeof(name), NULL, &size, &type, name);
        if (strcmp(name, "U") == 0 || strcmp(name, "U[0]") == 0) {
            pr->usize = (uint32_t)size * 16u;
        }
    }
    LEAVE();
}
void r36_glUseProgram(GLuint p) {
    ENTER();
    sCurProg = p;
    p_glUseProgram(p);
    LEAVE();
}

/* ---- buffers ---- */
void r36_glBindBuffer(GLenum target, GLuint b) {
    ENTER();
    if (target == GL_ARRAY_BUFFER) {
        sArrayBuf = b;
    } else if (target == GL_ELEMENT_ARRAY_BUFFER) {
        sElemBuf = b;
    }
    p_glBindBuffer(target, b);
    LEAVE();
}
void r36_glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage) {
    ENTER();
    GLuint b = target == GL_ARRAY_BUFFER ? sArrayBuf : target == GL_ELEMENT_ARRAY_BUFFER ? sElemBuf : 0;
    Buf *bf = b != 0 ? buf_find(b, 1) : NULL;
    if (bf != NULL) {
        bf->size = (uint32_t)size;
        if (bf->shadow != NULL) { /* (a mapped buffer given a new size: its copy made again) */
            free(bf->shadow);
            bf->shadow = NULL;
            bf->nr = 0;
        }
    }
    p_glBufferData(target, size, data, usage);
    LEAVE();
}
void r36_glDeleteBuffers(GLsizei n, const GLuint *b) {
    ENTER();
    for (GLsizei i = 0; i < n; i++) {
        Buf *bf = buf_find(b[i], 0);
        if (bf != NULL) {
            free(bf->shadow);
            *bf = sBufs[--sNBufs];
        }
    }
    p_glDeleteBuffers(n, b);
    LEAVE();
}
/* (vitaGL: the buffer's memory itself, for good; here a copy in main memory, see vglR36Dirty) */
void *r36_glMapBuffer(GLenum target, GLenum access) {
    ENTER_R(NULL);
    GLuint b = target == GL_ARRAY_BUFFER ? sArrayBuf : target == GL_ELEMENT_ARRAY_BUFFER ? sElemBuf : 0;
    Buf *bf;
    (void)access;
    bf = buf_find(b, 1);
    if (bf == NULL || b == 0) {
        LEAVE();
        return NULL;
    }
    if (bf->shadow == NULL && bf->size != 0) {
        bf->shadow = (uint8_t *)calloc(1, bf->size);
    }
    LEAVE();
    return bf->shadow;
}
GLboolean r36_glUnmapBuffer(GLenum target) { (void)target; return GL_TRUE; }
void vglR36Dirty(const void *at, size_t size) {
    const uint8_t *p = (const uint8_t *)at;
    int i;
    if (size == 0) {
        return;
    }
    for (i = 0; i < sNBufs; i++) {
        Buf *b = &sBufs[i];
        if (b->shadow != NULL && p >= b->shadow && p < b->shadow + b->size) {
            uint32_t lo = (uint32_t)(p - b->shadow), hi = lo + (uint32_t)size;
            int k;
            if (hi > b->size) {
                hi = b->size;
            }
            for (k = 0; k < b->nr; k++) { /* joined to a range it touches */
                if (lo <= b->r[k].hi && hi >= b->r[k].lo) {
                    if (lo < b->r[k].lo) { b->r[k].lo = lo; }
                    if (hi > b->r[k].hi) { b->r[k].hi = hi; }
                    sDirty = 1;
                    return;
                }
            }
            if (b->nr == 32) { /* (full: the nearest one grows) */
                int best = 0;
                uint32_t bd = 0xFFFFFFFFu;
                for (k = 0; k < 32; k++) {
                    uint32_t d = lo > b->r[k].hi ? lo - b->r[k].hi : b->r[k].lo - hi;
                    if (d < bd) { bd = d; best = k; }
                }
                if (lo < b->r[best].lo) { b->r[best].lo = lo; }
                if (hi > b->r[best].hi) { b->r[best].hi = hi; }
            } else {
                b->r[b->nr].lo = lo;
                b->r[b->nr].hi = hi;
                b->nr++;
            }
            sDirty = 1;
            return;
        }
    }
}
static void flush_dirty(void) {
    int i, k;
    if (!sDirty) {
        return;
    }
    sDirty = 0;
    for (i = 0; i < sNBufs; i++) {
        Buf *b = &sBufs[i];
        if (b->nr == 0) {
            continue;
        }
        p_glBindBuffer(GL_COPY_WRITE_BUFFER, b->id);
        for (k = 0; k < b->nr; k++) {
            p_glBufferSubData(GL_COPY_WRITE_BUFFER, (GLintptr)b->r[k].lo, (GLsizeiptr)(b->r[k].hi - b->r[k].lo), b->shadow + b->r[k].lo);
        }
        b->nr = 0;
    }
    p_glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
}

/* ---- vertex attributes ---- */
void r36_glEnableVertexAttribArray(GLuint i) {
    ENTER();
    if (i < 4) { sAttr[i].on = 1; }
    p_glEnableVertexAttribArray(i);
    LEAVE();
}
void r36_glDisableVertexAttribArray(GLuint i) {
    ENTER();
    if (i < 4) { sAttr[i].on = 0; }
    p_glDisableVertexAttribArray(i);
    LEAVE();
}
void r36_glVertexAttribPointer(GLuint i, GLint size, GLenum type, GLboolean norm, GLsizei stride, const void *ptr) {
    ENTER();
    if (i < 4) {
        Attr *a = &sAttr[i];
        a->set = 1; a->size = size; a->type = type; a->norm = norm; a->stride = stride; a->ptr = ptr; a->buf = sArrayBuf;
    }
    p_glVertexAttribPointer(i, size, type, norm, stride, ptr);
    LEAVE();
}
static void attr_restore(void) { /* the renderer's attributes 0..3 back as it set them */
    int i;
    for (i = 0; i < 4; i++) {
        Attr *a = &sAttr[i];
        if (a->set) {
            p_glBindBuffer(GL_ARRAY_BUFFER, a->buf);
            p_glVertexAttribPointer((GLuint)i, a->size, a->type, a->norm, a->stride, a->ptr);
        }
        if (a->on) {
            p_glEnableVertexAttribArray((GLuint)i);
        } else {
            p_glDisableVertexAttribArray((GLuint)i);
        }
    }
    p_glBindBuffer(GL_ARRAY_BUFFER, sArrayBuf);
}

/* ---- draws ---- */
static void pre_draw(void) {
    flush_dirty();
    if (sCurProg != 0 && (int)sCurProg < sProgCap) {
        Prog *p = &sProg[sCurProg];
        float f = sOnDisp ? -1.0f : 1.0f;
        if (p->flip >= 0 && p->flipVal != f) {
            p_glUniform1f(p->flip, f);
            p->flipVal = f;
        }
    }
}
/* a draw with program 0: the fixed-function pipeline's way, with our shader */
static void ff_attr(int i, const Attr *a, int on) {
    if (on && a->ptr != NULL) {
        p_glBindBuffer(GL_ARRAY_BUFFER, 0);
        p_glVertexAttribPointer((GLuint)i, a->size, a->type, a->type == GL_UNSIGNED_BYTE ? GL_TRUE : GL_FALSE, a->stride, a->ptr);
        p_glEnableVertexAttribArray((GLuint)i);
    } else {
        p_glDisableVertexAttribArray((GLuint)i);
    }
}
static void ff_begin(void) {
    static const float kEnv[4] = {0, 1, 2, 3};
    int env = !sFF.tex2d || !sFF.ta ? 0 : sFF.env == GL_REPLACE ? 2 : sFF.env == GL_DECAL ? 3 : 1;
    int af = 0;
    if (sFF.atest) {
        switch (sFF.afunc) {
        case GL_NEVER: af = 1; break; case GL_ALWAYS: af = 2; break; case GL_LESS: af = 3; break; case GL_LEQUAL: af = 4; break;
        case GL_EQUAL: af = 5; break; case GL_GEQUAL: af = 6; break; case GL_GREATER: af = 7; break; default: af = 8; break;
        }
    }
    p_glUseProgram(sFFProg);
    p_glUniform1f(sFFU[0], sOnDisp ? -1.0f : 1.0f);
    p_glUniform4f(sFFU[1], sFF.color[0], sFF.color[1], sFF.color[2], sFF.color[3]);
    p_glUniform1f(sFFU[2], sFF.ca ? 1.0f : 0.0f);
    p_glUniform1i(sFFU[3], 0);
    p_glUniform4f(sFFU[4], kEnv[env], (float)af, sFF.aref, 0.0f);
    ff_attr(0, &sFF.v, sFF.va);
    ff_attr(1, &sFF.c, sFF.ca);
    ff_attr(2, &sFF.t, sFF.ta);
    p_glDisableVertexAttribArray(3);
}
static void ff_end(void) {
    p_glUseProgram(sCurProg);
    attr_restore();
}
void r36_glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    ENTER();
    pre_draw();
    if (sCurProg == 0) {
        ff_begin();
        p_glDrawArrays(mode, first, count);
        ff_end();
    } else {
        p_glDrawArrays(mode, first, count);
    }
    LEAVE();
}
void r36_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *idx) {
    ENTER();
    pre_draw();
    if (sCurProg == 0) {
        ff_begin();
        p_glDrawElements(mode, count, type, idx);
        ff_end();
    } else {
        p_glDrawElements(mode, count, type, idx);
    }
    LEAVE();
}
void r36_glAlphaFunc(GLenum func, GLfloat ref) { sFF.afunc = func; sFF.aref = ref; }
void r36_glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { sFF.color[0] = r; sFF.color[1] = g; sFF.color[2] = b; sFF.color[3] = a; }
static void ff_ptr(Attr *a, GLint size, GLenum type, GLsizei stride, const void *ptr) { a->size = size; a->type = type; a->stride = stride; a->ptr = ptr; }
void r36_glVertexPointer(GLint size, GLenum type, GLsizei stride, const void *ptr) { ff_ptr(&sFF.v, size, type, stride, ptr); }
void r36_glColorPointer(GLint size, GLenum type, GLsizei stride, const void *ptr) { ff_ptr(&sFF.c, size, type, stride, ptr); }
void r36_glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *ptr) { ff_ptr(&sFF.t, size, type, stride, ptr); }
static void client(GLenum a, int on) {
    if (a == GL_VERTEX_ARRAY) { sFF.va = on; }
    else if (a == GL_COLOR_ARRAY) { sFF.ca = on; }
    else if (a == GL_TEXTURE_COORD_ARRAY) { sFF.ta = on; }
}
void r36_glEnableClientState(GLenum a) { client(a, 1); }
void r36_glDisableClientState(GLenum a) { client(a, 0); }
void r36_glTexEnvi(GLenum target, GLenum pname, GLint param) { (void)target; if (pname == GL_TEXTURE_ENV_MODE) { sFF.env = (GLenum)param; } }
void r36_glMatrixMode(GLenum mode) { (void)mode; }
void r36_glLoadIdentity(void) { }

/* ---- vitaGL's own ---- */
GLboolean vglFastPrepare(GLenum type) { (void)type; return sCurProg != 0 && sElemBuf != 0; }
void vglFastDraw(GLenum type, uint32_t offset, uint32_t count, const void *vunif, uint32_t size) {
    ENTER();
    Prog *p = (int)sCurProg < sProgCap ? &sProg[sCurProg] : NULL;
    pre_draw();
    if (p != NULL && p->u >= 0) {
        p_glUniform4fv(p->u, (GLsizei)(size / 16u), (const GLfloat *)vunif);
    }
    p_glDrawElements(GL_TRIANGLES, (GLsizei)count, type, (const void *)(uintptr_t)offset);
    LEAVE();
}
void vglFastDrawPtr(GLenum type, const void *idx, uint32_t count, const void *vunif, uint32_t size) {
    ENTER();
    Prog *p = (int)sCurProg < sProgCap ? &sProg[sCurProg] : NULL;
    pre_draw();
    if (p != NULL && p->u >= 0) {
        p_glUniform4fv(p->u, (GLsizei)(size / 16u), (const GLfloat *)vunif);
    }
    p_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); /* (indices in main memory) */
    p_glDrawElements(GL_TRIANGLES, (GLsizei)count, type, idx);
    p_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sElemBuf);
    LEAVE();
}
uint32_t vglFastVertUniformSize(GLuint prog) { return (int)prog < sProgCap ? sProg[prog].usize : 0; }
int vglFastUniformOffset(GLuint prog, GLint location) {
    if (location < 0 || prog == 0 || (int)prog >= sProgCap) {
        return -1;
    }
    return location == sProg[prog].u ? 0 : -1;
}
/* scratch memory for one picture's indices (vglFastDrawPtr): a ring, made over at each swap */
static uint8_t *sScratch;
static size_t sScratchAt;
#define SCRATCH (4u << 20)
void *vglAllocFromScratch(size_t size) {
    void *p;
    if (sScratch == NULL) {
        sScratch = (uint8_t *)malloc(SCRATCH);
    }
    size = (size + 15) & ~(size_t)15;
    if (sScratchAt + size > SCRATCH) {
        sScratchAt = 0;
    }
    p = sScratch + sScratchAt;
    sScratchAt += size;
    return p;
}
/* (a picture is not handed to the chip in parts here: the renderer then reads the display through its own copies) */
GLboolean vglSplitScene(void) { return GL_FALSE; }
void vglBt3ContNext(void) { }
void vglBt3Dirty(void) { }
void vglSetupScratchMemory(GLboolean v, GLboolean f) { (void)v; (void)f; }
void vglSetVertexBufferSize(uint32_t s) { (void)s; }
void vglSetVDMBufferSize(uint32_t s) { (void)s; }
void vglSetFragmentBufferSize(uint32_t s) { (void)s; }
void vglSetParamBufferSize(uint32_t s) { (void)s; }
void vglSetupRenderTargetScenesNum(uint8_t a, uint8_t b) { (void)a; (void)b; }
void vglWaitVblankStart(GLboolean e) { (void)e; }
void vglSetDisplayCallback(void (*cb)(void *)) { sDisplayCb = cb; }
void vglSwapResolution(int w, int h) { sNewW = w; sNewH = h; }

GLboolean vglInitExtended(int legacy_pool_size, int width, int height, int ram_threshold, SceGxmMultisampleMode msaa) {
    int i;
    (void)legacy_pool_size; (void)ram_threshold; (void)msaa;
    if (!R36_VideoInit()) {
        return GL_FALSE;
    }
    gl_make_primary(); /* (the thread that starts the renderer draws) */
    ctx_take();
    if (sDispMem == NULL) {
        sDispMem = (uint8_t *)calloc(1, 960u * 544u * 4u + 64u);
    }
    for (i = 0; i < NDISP; i++) {
        gxm_color_surfaces_addr[i] = sDispMem + i; /* (distinct addresses; nothing is drawn there) */
    }
    disp_alloc(width, height);
    gxm_back_buffer_index = 0;
    gxm_front_buffer_index = NDISP - 1;
    sOnDisp = 1;
    sVp[0] = sVp[1] = 0; sVp[2] = width; sVp[3] = height;
    memcpy(sSc, sVp, sizeof(sSc));
    bind_target();
    p_glClearColor(0, 0, 0, 1);
    p_glClearDepthf(0.0f);
    p_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    p_glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    return GL_TRUE;
}

/* the finished buffer on the screen: the picture as large as fits with its own shape, turned the right way up */
static float sAspect = 0.0f; /* (the shape the picture is shown at, width / height; 0: the display buffer's) */
void R36_SetAspect(float a) { sAspect = a; }
static void present_tex(GLuint tex, int w, int h, int flipped) {
    static const float kQuad[8] = {-1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};
    float a = sAspect > 0.0f ? sAspect : (float)w / (float)h, sw = 1.0f, sh = 1.0f;
    if ((float)sWinW / (float)sWinH > a) {
        sw = a * (float)sWinH / (float)sWinW;
    } else {
        sh = (float)sWinW / a / (float)sWinH;
    }
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    p_glViewport(0, 0, sWinW, sWinH);
    p_glDisable(GL_SCISSOR_TEST);
    p_glDisable(GL_BLEND);
    p_glDisable(GL_DEPTH_TEST);
    p_glDisable(GL_STENCIL_TEST);
    p_glDisable(GL_CULL_FACE);
    p_glColorMask(1, 1, 1, 1);
    p_glClearColor(0, 0, 0, 1);
    p_glClear(GL_COLOR_BUFFER_BIT);
    p_glUseProgram(sPresentProg);
    p_glUniform4f(sPresentU[0], sw, flipped ? sh : -sh, 0.0f, 0.0f);
    p_glUniform1i(sPresentU[1], 0);
    p_glActiveTexture(GL_TEXTURE0);
    p_glBindTexture(GL_TEXTURE_2D, tex);
    p_glBindBuffer(GL_ARRAY_BUFFER, 0);
    p_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, kQuad);
    p_glEnableVertexAttribArray(0);
    p_glDisableVertexAttribArray(1);
    p_glDisableVertexAttribArray(2);
    p_glDisableVertexAttribArray(3);
    p_glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    SDL_GL_SwapWindow(sWin);
}
/* everything present_tex changed, back as the renderer had it */
static void state_restore(void) {
    p_glClearColor(0, 0, 0, 1);
    p_glColorMask(sMask[0], sMask[1], sMask[2], sMask[3]);
    if (sScOn) { p_glEnable(GL_SCISSOR_TEST); }
    if (sCapBlend) { p_glEnable(GL_BLEND); }
    if (sCapDepth) { p_glEnable(GL_DEPTH_TEST); }
    if (sCapStencil) { p_glEnable(GL_STENCIL_TEST); }
    if (sCapCull) { p_glEnable(GL_CULL_FACE); }
    p_glActiveTexture(GL_TEXTURE0 + (GLenum)sUnit);
    p_glBindTexture(GL_TEXTURE_2D, real_tex(sBound[sUnit & 3]));
    if (sUnit != 0) {
        p_glActiveTexture(GL_TEXTURE0);
        p_glBindTexture(GL_TEXTURE_2D, real_tex(sBound[0]));
        p_glActiveTexture(GL_TEXTURE0 + (GLenum)sUnit);
    }
    p_glUseProgram(sCurProg);
    attr_restore();
    bind_target();
}
unsigned gR36Frames;
void vglSwapBuffers(GLboolean has_commondialog) {
    ENTER();
    unsigned done = gxm_back_buffer_index % NDISP;
    (void)has_commondialog;
    flush_dirty();
    if (sDisplayCb != NULL) {
        sDisplayCb(gxm_color_surfaces_addr[done]); /* (the renderer's pacing: it may wait for its blank here) */
    }
    {
        static unsigned lastVc;
        unsigned vc = (unsigned)sceDisplayGetVcount();
        if (lastVc != 0) {
            unsigned d = vc - lastVc;
            vgl_flip_hist[d >= 4 ? 4 : d]++;
        }
        lastVc = vc;
    }
    present_tex(sDispTex[done], sDispW, sDispH, 1);
    gR36Frames++;
    {
        extern int gR36TextOn;
        gR36TextOn = 0;
    }
    gxm_front_buffer_index = done;
    gxm_back_buffer_index = (done + 1) % NDISP;
    sScratchAt = 0;
    if (sNewW > 0 && (sNewW != sDispW || sNewH != sDispH)) {
        disp_alloc(sNewW, sNewH);
    }
    sNewW = sNewH = 0;
    state_restore();
    LEAVE();
}

/* ---- the text screen (r36s_text.c): its picture on the screen, from whichever thread prints ---- */
static GLuint sTextTex;
void R36_TextPresent(const uint32_t *px, int w, int h) {
    int f = gl_enter();
    if (f < 0 || !sVideoUp) {
        return;
    }
    if (sTextTex == 0) {
        p_glGenTextures(1, &sTextTex);
        p_glBindTexture(GL_TEXTURE_2D, sTextTex);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    p_glActiveTexture(GL_TEXTURE0);
    p_glBindTexture(GL_TEXTURE_2D, sTextTex);
    p_glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    p_glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    {
        float keep = sAspect;
        sAspect = (float)w / (float)h;
        present_tex(sTextTex, w, h, 1);
        present_tex(sTextTex, w, h, 1); /* (both buffers of the window: it stays up) */
        sAspect = keep;
    }
    if (sDispTex[0] != 0) {
        state_restore();
    }
    gl_leave(f);
}

/* the passthroughs */
#define R36_GL_PASS(r, n, p, a) r r36_##n p { ENTER_R((r)0); r v_ = p_##n a; LEAVE(); return v_; }
#define R36_GL_PASS_V(n, p, a) void r36_##n p { ENTER(); p_##n a; LEAVE(); }
R36_GL_PASS_V(glAttachShader, (GLuint program, GLuint shader), (program, shader))
R36_GL_PASS_V(glBindAttribLocation, (GLuint program, GLuint index, const GLchar *name), (program, index, name))
R36_GL_PASS_V(glBlendEquation, (GLenum mode), (mode))
R36_GL_PASS_V(glBlendEquationSeparate, (GLenum modeRGB, GLenum modeAlpha), (modeRGB, modeAlpha))
R36_GL_PASS_V(glBlendFunc, (GLenum sfactor, GLenum dfactor), (sfactor, dfactor))
R36_GL_PASS_V(glBlendFuncSeparate, (GLenum a, GLenum b, GLenum c, GLenum d), (a, b, c, d))
R36_GL_PASS_V(glClearColor, (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha), (red, green, blue, alpha))
R36_GL_PASS_V(glClearDepthf, (GLfloat d), (d))
R36_GL_PASS_V(glClearStencil, (GLint s), (s))
R36_GL_PASS(GLuint, glCreateProgram, (void), ())
R36_GL_PASS_V(glDepthFunc, (GLenum func), (func))
R36_GL_PASS_V(glDepthMask, (GLboolean flag), (flag))
R36_GL_PASS_V(glDeleteFramebuffers, (GLsizei n, const GLuint *framebuffers), (n, framebuffers))
R36_GL_PASS_V(glFinish, (void), ())
R36_GL_PASS_V(glFlush, (void), ())
R36_GL_PASS_V(glGenFramebuffers, (GLsizei n, GLuint *framebuffers), (n, framebuffers))
R36_GL_PASS_V(glGetShaderInfoLog, (GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog), (shader, bufSize, length, infoLog))
R36_GL_PASS_V(glGetShaderiv, (GLuint shader, GLenum pname, GLint *params), (shader, pname, params))
R36_GL_PASS_V(glGetProgramiv, (GLuint program, GLenum pname, GLint *params), (program, pname, params))
R36_GL_PASS_V(glGetProgramInfoLog, (GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog), (program, bufSize, length, infoLog))
R36_GL_PASS(GLint, glGetUniformLocation, (GLuint program, const GLchar *name), (program, name))
R36_GL_PASS_V(glStencilFunc, (GLenum func, GLint ref, GLuint mask), (func, ref, mask))
R36_GL_PASS_V(glStencilMask, (GLuint mask), (mask))
R36_GL_PASS_V(glStencilOp, (GLenum fail, GLenum zfail, GLenum zpass), (fail, zfail, zpass))
R36_GL_PASS_V(glUniform1i, (GLint location, GLint v0), (location, v0))
R36_GL_PASS_V(glUniform1f, (GLint location, GLfloat v0), (location, v0))
R36_GL_PASS_V(glUniform4f, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3), (location, v0, v1, v2, v3))
R36_GL_PASS_V(glUniform4fv, (GLint location, GLsizei count, const GLfloat *value), (location, count, value))
R36_GL_PASS_V(glGenBuffers, (GLsizei n, GLuint *buffers), (n, buffers))
R36_GL_PASS(GLenum, glGetError, (void), ())
R36_GL_PASS(GLenum, glCheckFramebufferStatus, (GLenum target), (target))
