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
#include <dlfcn.h>
#include <math.h>
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
    X(void, glBindVertexArray, (GLuint)) X(void *, glMapBufferRange, (GLenum, GLintptr, GLsizeiptr, GLbitfield)) X(GLboolean, glUnmapBuffer, (GLenum)) \
    X(void, glDrawElementsBaseVertex, (GLenum, GLsizei, GLenum, const void *, GLint)) \
    X(void, glWaitSync, (GLsync, GLbitfield, GLuint64)) X(void, glInvalidateFramebuffer, (GLenum, GLsizei, const GLenum *)) \
    X(void, glBindBufferRange, (GLenum, GLuint, GLuint, GLintptr, GLsizeiptr)) X(GLuint, glGetUniformBlockIndex, (GLuint, const GLchar *)) \
    X(void, glUniformBlockBinding, (GLuint, GLuint, GLuint)) X(void, glGetActiveUniformsiv, (GLuint, GLsizei, const GLuint *, GLenum, GLint *)) \
    X(void, glGetActiveUniformBlockiv, (GLuint, GLuint, GLenum, GLint *)) X(void, glGetAttachedShaders, (GLuint, GLsizei, GLsizei *, GLuint *)) \
    X(GLsync, glFenceSync, (GLenum, GLbitfield)) X(GLenum, glClientWaitSync, (GLsync, GLbitfield, GLuint64)) X(void, glDeleteSync, (GLsync)) \
    X(void, glBufferStorageEXT, (GLenum, GLsizeiptr, const void *, GLbitfield))
#define X(r, n, p) static r(*p_##n) p;
R36_REAL_LIST
#undef X

/* ---- the state as the driver has it: a call that would set what is already set is left out (the renderer sets
   the same buffer, attribute pointers and program again and again: some 30000 calls a second into the driver) ---- */
#define R_UNK 0xFFFFFFFFu
/* the changes of state the driver sees, for the log: programs, framebuffers, textures, buffers, uniform bytes, capabilities */
unsigned long long gR36Chg[6];
/* (for the log) what happened since the last draw, and the draws that took long by it: 1 framebuffer, 2 program,
   4 texture bound, 8 texture written, 16 blend/depth/stencil/mask/capability, 32 uniforms, 64 texture parameters,
   128 the picture's first draw, 256 a clear */
static unsigned sEv = 128;
#define EV_N 512
static unsigned sSlowN[EV_N], sAllN[EV_N];
static unsigned long long sSlowNs[EV_N], sAllNs[EV_N];
static unsigned ev_name(const char *n) {
    if (strstr(n, "Uniform")) { return 32; }
    if (strstr(n, "Blend") || strstr(n, "Depth") || strstr(n, "Stencil") || strstr(n, "Mask")) { return 16; }
    return 0;
}
typedef struct RAttr { GLuint buf; GLint size; GLenum type; GLboolean norm; GLsizei stride; const void *ptr; signed char valid, on; } RAttr;
static GLuint rBuf[2] = {R_UNK, R_UNK}, rProg = R_UNK, rFbo = R_UNK;
static RAttr rAt[8];
/* (TrimUI) the driver's way of doing less: 1 the array buffer bound only when an attribute pointer or the buffer's data
   needs it (a draw does not read that binding), the texture parameters a texture already has left out, the stream's
   vertices at one place for the attributes (a draw's first vertex instead of new pointers), the stage's runs of state
   that may go in any order sorted by program and texture. 0: as before. vita_vgl.c sets it (bt3.txt opt, optab). */
int gR36Opt = 1;
static GLuint rArrWant = R_UNK; /* the array buffer the renderer has bound (rBuf[0]: the driver's) */
static inline void r_arr_sync(void) {
    if (rArrWant != R_UNK && rBuf[0] != rArrWant) {
        p_glBindBuffer(GL_ARRAY_BUFFER, rArrWant);
        rBuf[0] = rArrWant;
        gR36Chg[3]++;
    }
}
static void r_forget(void) {
    int i;
    rBuf[0] = rBuf[1] = rProg = rFbo = R_UNK;
    for (i = 0; i < 8; i++) {
        rAt[i].valid = 0;
        rAt[i].on = -1;
    }
}
__attribute__((constructor)) static void r_init(void) { r_forget(); }
static inline void r_bind(GLenum t, GLuint b) {
    int k = t == GL_ARRAY_BUFFER ? 0 : t == GL_ELEMENT_ARRAY_BUFFER ? 1 : -1;
    if (k == 0) {
        rArrWant = b;
        if (!gR36Opt) {
            r_arr_sync();
        }
    } else if (k < 0) {
        p_glBindBuffer(t, b);
    } else if (rBuf[k] != b) {
        p_glBindBuffer(t, b);
        rBuf[k] = b;
        gR36Chg[3]++;
    }
}
static inline void r_vap(GLuint i, GLint size, GLenum type, GLboolean norm, GLsizei stride, const void *ptr) {
    RAttr *a;
    r_arr_sync();
    if (i >= 8) {
        p_glVertexAttribPointer(i, size, type, norm, stride, ptr);
        return;
    }
    a = &rAt[i];
    if (a->valid && a->buf == rBuf[0] && a->size == size && a->type == type && a->norm == norm && a->stride == stride && a->ptr == ptr) {
        return;
    }
    p_glVertexAttribPointer(i, size, type, norm, stride, ptr);
    a->valid = rBuf[0] != R_UNK;
    a->buf = rBuf[0]; a->size = size; a->type = type; a->norm = norm; a->stride = stride; a->ptr = ptr;
}
static inline void r_attr_on(GLuint i, int on) {
    if (i < 8 && rAt[i].on == on) {
        return;
    }
    if (on) {
        p_glEnableVertexAttribArray(i);
    } else {
        p_glDisableVertexAttribArray(i);
    }
    if (i < 8) {
        rAt[i].on = (signed char)on;
    }
}
static inline void r_use(GLuint p) {
    if (rProg != p) {
        p_glUseProgram(p);
        rProg = p;
        gR36Chg[0]++;
        sEv |= 2;
    }
}
static inline void r_fbo(GLuint f) {
    if (rFbo != f) {
        p_glBindFramebuffer(GL_FRAMEBUFFER, f);
        rFbo = f;
        gR36Chg[1]++;
        sEv |= 1;
    }
}

/* ---- the window and the context ---- */
static SDL_Window *sWin;
static SDL_GLContext sCtx;
static int sWinW = 640, sWinH = 480;
static int sVideoUp;
int gR36Vsync = 1;

/* ---- who holds the context ----
   One GL context, current on one thread at a time. A thread that makes a GL call takes it (waiting while another
   has it) and keeps it, so that a run of calls costs nothing more; it lets it go when it is about to wait (a
   semaphore, a sleep, a mutex held elsewhere: R36_GlRelease from r36s_psp2.c), when it hands work to the render
   thread (vita_vgl.c's ring) or when another thread asks for it at its next call. The renderer's render thread
   (port/vita/vita_vgl.c rt_*) makes the draws; the drawing thread waits for it to be done (rt_sync) before a GL
   call of its own, as on the Vita, so the two never interleave their calls. */
static pthread_mutex_t sCtxLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sCtxCond = PTHREAD_COND_INITIALIZER;
static int sCtxHeld;          /* a thread holds it */
static volatile int sCtxWant; /* threads waiting for it */
static pthread_t sCtxOwner;   /* (for the log: who has it) */
static __thread int tHold;    /* this thread holds it */
static __thread int tKeep;    /* this thread keeps it while it waits (the render thread: asked for it, it lets go) */
static __thread int tSticky;  /* this thread keeps it between calls (the drawing and render threads); others for one call */
void (*gR36CtxWake)(void);    /* (vita_vgl.c) the keeper asked to let go of it */
unsigned long long gR36Ctx[4]; /* for the log: times taken, us waiting for it, us making it current, times given up */
unsigned long long R36_NowNs(void);

/* (the present thread, see present_*) the renderer's context made current without the window: the window's surface is the
   present thread's */
static int sPresentOn;
static struct {
    void *dpy, *ctx, *ctxP, *surf;
    unsigned (*MakeCurrent)(void *, void *, void *, void *);
    unsigned (*SwapBuffers)(void *, void *);
    unsigned (*SwapInterval)(void *, int);
} sE;
static void present_init(void);
static void ctx_current(int on) {
    if (sPresentOn) {
        sE.MakeCurrent(sE.dpy, NULL, NULL, on ? sE.ctx : NULL);
    } else {
        SDL_GL_MakeCurrent(sWin, on ? sCtx : NULL);
    }
}
static void ctx_take(void) {
    struct timespec ts;
    int timedOut = 0;
    unsigned long long t0 = R36_NowNs(), t1;
    pthread_mutex_lock(&sCtxLock);
    sCtxWant++;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 3;
    if (sCtxHeld && gR36CtxWake != NULL) {
        pthread_mutex_unlock(&sCtxLock);
        gR36CtxWake();
        pthread_mutex_lock(&sCtxLock);
    }
    while (sCtxHeld && !timedOut) {
        timedOut = pthread_cond_timedwait(&sCtxCond, &sCtxLock, &ts) != 0;
    }
    sCtxWant--;
    if (sCtxHeld) { /* the holder did not let it go in 3 s: nothing to do but go on without it */
        char who[32] = "?", me[32] = "?";
        pthread_getname_np(sCtxOwner, who, sizeof(who));
        pthread_getname_np(pthread_self(), me, sizeof(me));
        pthread_mutex_unlock(&sCtxLock);
        R36_LogLine("r36 gl: the GL context was not given up by the thread holding it (%s) in 3 s; %s goes on without it\n", who, me);
        return;
    }
    sCtxHeld = 1;
    sCtxOwner = pthread_self();
    pthread_mutex_unlock(&sCtxLock);
    t1 = R36_NowNs();
    ctx_current(1);
    tHold = 1;
    gR36Ctx[0]++;
    gR36Ctx[1] += (t1 - t0) / 1000u;
    gR36Ctx[2] += (R36_NowNs() - t1) / 1000u;
}
static void ctx_give(void) {
    ctx_current(0);
    tHold = 0;
    gR36Ctx[3]++;
    pthread_mutex_lock(&sCtxLock);
    sCtxHeld = 0;
    pthread_cond_broadcast(&sCtxCond);
    pthread_mutex_unlock(&sCtxLock);
}
/* this thread has handed its work over (or was asked for the context): the context to whoever wants it */
void R36_GlRelease(void) {
    if (tHold) {
        ctx_give();
    }
}
/* this thread is about to wait: the context let go, but by the keeper (it lets go when asked) */
void R36_GlBlocking(void) {
    if (tHold && !tKeep) {
        ctx_give();
    }
}
void R36_GlKeep(void) { tKeep = 1; tSticky = 1; }
int R36_GlWanted(void) { return sCtxWant != 0; }
/* a moment's pause (sched_yield): the context only if another thread waits for it */
void R36_GlYield(void) {
    if (tHold && sCtxWant) {
        ctx_give();
    }
}
/* 0: go on (1: the context taken for this call only, let go at gl_leave); -1: no context */
static inline int gl_enter(void) {
    if (__builtin_expect(tHold, 1)) {
        if (__builtin_expect(sCtxWant != 0, 0) && tSticky) { /* (another thread waits: it goes first) */
            ctx_give();
            sched_yield();
        } else {
            return 0;
        }
    }
    ctx_take();
    return !tHold ? -1 : tSticky ? 0 : 1;
}
static inline void gl_leave(int f) {
    if (f == 1) {
        ctx_give();
    }
}
/* the time each GL call takes on the clock (in the driver, waiting included), by function: prof.txt (r36s_prof.c) */
typedef struct GlT { const char *name; unsigned long long ns, max; unsigned n, slow; } GlT;
#define GLT_MAX 192
static GlT sGlT[GLT_MAX], sGlTPrev[GLT_MAX];
static int sGlTn;
static pthread_mutex_t sGlTLock = PTHREAD_MUTEX_INITIALIZER;
unsigned long long R36_NowNs(void); /* (r36s_psp2.c: the processor's counter) */
static inline unsigned long long glt_now(void) { /* (the system's clock itself: R36_NowNs may be a 0.5 ms tick) */
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned long long)t.tv_sec * 1000000000ull + (unsigned long long)t.tv_nsec;
}

static int sGltOn = -1;
static inline unsigned long long glt_start(void) {
    if (__builtin_expect(sGltOn == 0, 1)) {
        return 0;
    }
    if (sGltOn < 0) {
        const char *e = getenv("R36_GLTIME");
        sGltOn = e != NULL && atoi(e) != 0;
        if (!sGltOn) {
            return 0;
        }
    }
    return glt_now();
}
static void glt_add(int *slot, const char *name, unsigned long long t0) {
    unsigned long long d;
    GlT *g;
    if (__builtin_expect(t0 == 0, 1)) {
        return;
    }
    d = glt_now() - t0;
    if (*slot < 0) {
        pthread_mutex_lock(&sGlTLock);
        if (*slot < 0 && sGlTn < GLT_MAX) {
            sGlT[sGlTn].name = name;
            *slot = sGlTn++;
        }
        pthread_mutex_unlock(&sGlTLock);
        if (*slot < 0) {
            return;
        }
    }
    g = &sGlT[*slot];
    g->ns += d;
    g->n++;
    if (d > g->max) {
        g->max = d;
    }
    if (d > 2000000ull) {
        g->slow++;
    }
}
static int glt_cmp(const void *a, const void *b) {
    unsigned long long x = ((const GlT *)a)->ns, y = ((const GlT *)b)->ns;
    return x < y ? 1 : x > y ? -1 : 0;
}
/* what changed since the last call, the largest first */
void R36_GlTimes(FILE *f, double secs) {
    GlT d[GLT_MAX];
    int i, n = sGlTn, k = 0;
    for (i = 0; i < n; i++) {
        GlT c = sGlT[i];
        d[k].name = c.name;
        d[k].ns = c.ns - sGlTPrev[i].ns;
        d[k].n = c.n - sGlTPrev[i].n;
        d[k].slow = c.slow - sGlTPrev[i].slow;
        d[k].max = c.max;
        sGlT[i].max = 0;
        sGlTPrev[i] = c;
        if (d[k].n != 0) {
            k++;
        }
    }
    qsort(d, k, sizeof(GlT), glt_cmp);
    {
        static unsigned long long prev[4];
        unsigned long long c[4];
        int j;
        for (j = 0; j < 4; j++) {
            c[j] = gR36Ctx[j] - prev[j];
            prev[j] = gR36Ctx[j];
        }
        fprintf(f, "GL context: taken %.0f times a second, waiting for it %.1f ms a second, making it current %.1f ms a second (%.0f us each)\n",
                c[0] / secs, c[1] / 1000.0 / secs, c[2] / 1000.0 / secs, c[0] ? (double)c[2] / (double)c[0] : 0.0);
    }
    {
        extern unsigned gR36Dpb[3], gR36Frames, gR36Draws; /* (vita_vgl.c: the batched direct path) */
        static unsigned prev[5];
        unsigned c[5] = {gR36Dpb[0] - prev[0], gR36Dpb[1] - prev[1], gR36Dpb[2] - prev[2], gR36Draws - prev[3], gR36Frames - prev[4]};
        prev[0] = gR36Dpb[0]; prev[1] = gR36Dpb[1]; prev[2] = gR36Dpb[2]; prev[3] = gR36Draws; prev[4] = gR36Frames;
        fprintf(f, "draws: %.0f a second, %.0f per picture (%.1f pictures a second) | model parts batched: %.0f a second in %.0f draws, %.0f clashes\n",
                c[3] / secs, c[4] ? (double)c[3] / c[4] : 0.0, c[4] / secs, c[0] / secs, c[1] / secs, c[2] / secs);
    }
    {
        static unsigned long long prev[6];
        unsigned long long c[6];
        int j;
        for (j = 0; j < 6; j++) {
            c[j] = gR36Chg[j] - prev[j];
            prev[j] = gR36Chg[j];
        }
        fprintf(f, "state changes a second: programs %.0f, framebuffers %.0f, textures bound %.0f, buffers bound %.0f, uniform KB %.0f, blend/depth/other %.0f\n",
                c[0] / secs, c[1] / secs, c[2] / secs, c[3] / secs, c[4] / 1024.0 / secs, c[5] / secs);
    }
    {   /* the draws that took over 1 ms, by what changed just before them */
        static const char kL[9] = {'F', 'P', 'T', 'W', 'S', 'U', 'Q', '1', 'C'};
        static unsigned pN[EV_N], pA[EV_N];
        static unsigned long long pNs[EV_N], pANs[EV_N];
        unsigned dn[EV_N], an[EV_N], idx[EV_N], tot = 0, totA = 0, m;
        unsigned long long dns[EV_N], ans[EV_N], totNs = 0;
        int j, nI = 0;
        for (j = 0; j < EV_N; j++) {
            dn[j] = sSlowN[j] - pN[j]; pN[j] = sSlowN[j];
            dns[j] = sSlowNs[j] - pNs[j]; pNs[j] = sSlowNs[j];
            an[j] = sAllN[j] - pA[j]; pA[j] = sAllN[j];
            ans[j] = sAllNs[j] - pANs[j]; pANs[j] = sAllNs[j];
            tot += dn[j];
            totNs += dns[j];
            totA += an[j];
            if (dn[j] != 0) {
                idx[nI++] = (unsigned)j;
            }
        }
        for (j = 1; j < nI; j++) { /* (by time, the most first) */
            unsigned x = idx[j];
            int k2 = j - 1;
            while (k2 >= 0 && dns[idx[k2]] < dns[x]) {
                idx[k2 + 1] = idx[k2];
                k2--;
            }
            idx[k2 + 1] = x;
        }
        fprintf(f, "draws over 1 ms: %.0f a second of %.0f, %.0f ms a second; by what changed before them (F framebuffer, P program, T texture bound, W texture written, S blend/depth/stencil, U uniforms, Q texture parameters, 1 picture's first, C clear):",
                tot / secs, totA / secs, totNs / 1e6 / secs);
        for (j = 0; j < nI && j < 12; j++) {
            char lab[12];
            int q = 0, b;
            m = idx[j];
            for (b = 0; b < 9; b++) {
                if (m & (1u << b)) {
                    lab[q++] = kL[b];
                }
            }
            if (q == 0) {
                lab[q++] = '-';
            }
            lab[q] = 0;
            fprintf(f, " %s %.0f/s %.0f ms (of %.0f/s, %.0f ms);", lab, dn[m] / secs, dns[m] / 1e6 / secs, an[m] / secs, ans[m] / 1e6 / secs);
        }
        fprintf(f, "\n");
    }
    if (sGltOn != 1) {
        return;
    }
    fprintf(f, "GL calls on the clock (waiting included), ms per second: ");
    for (i = 0; i < k && i < 30; i++) {
        fprintf(f, "%s%s %.1f (%u calls/s, longest %.1f ms, %u over 2 ms)", i ? "; " : "", strncmp(d[i].name, "r36_", 4) == 0 ? d[i].name + 4 : d[i].name, d[i].ns / 1e6 / secs, (unsigned)(d[i].n / secs),
                d[i].max / 1e6, d[i].slow);
    }
    fprintf(f, "\n");
}
#define ENTER() static int glt_slot_ = -1; unsigned long long glt_t0_ = glt_start(); int gl_f_ = gl_enter(); if (gl_f_ < 0) { return; }
#define ENTER_R(v) static int glt_slot_ = -1; unsigned long long glt_t0_ = glt_start(); int gl_f_ = gl_enter(); if (gl_f_ < 0) { return v; }
#define LEAVE() (gl_leave(gl_f_), glt_add(&glt_slot_, __func__, glt_t0_))

/* ---- state the shim keeps track of ---- */
#define NDISP 3
typedef struct VTex {
    GLuint real;
    int alias;              /* 0 its own, 1..3 display buffer n-1, 4 the depth buffer */
    int used;
    GLint tp[4];            /* (gR36Opt) its filters and wraps as last set; 0 not known */
    int tw, th;             /* its level 0's size when made RGBA8 by glTexImage2D (0: none yet), see the pool */
    SceGxmTexture gxm;      /* (vglGetGxmTexture: a stand-in nothing reads) */
} VTex;
static VTex *sTex;
static int sTexCap, sTexFree = -1;
static GLuint sBound[4];    /* the renderer's texture per unit */
static int sUnit;

static GLuint sDispTex[NDISP], sDispFbo[NDISP], sDepthTexA[NDISP];
/* (each display buffer its own depth: a picture's depth is read (the fog) while the next one is drawn) */
#define sDepthTex sDepthTexA[gxm_back_buffer_index % NDISP]
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

/* programs: where their r36_flip and U are.
   (TrimUI) The uniforms of the renderer's shaders in uniform blocks: on the PowerVR driver each glUniform* before a
   draw cost ~30 us (r36s_bench.c), a uniform buffer's range bound ~6 us. The Cg shaders' uniforms (not their
   samplers) go into a block of each stage (R36V, binding 0; R36F, binding 1: ub_block); glUniform* writes a copy
   here (a location of ours, UB_LOC), and before a draw a changed copy is put into the stream and its range bound.
   R36_UBO=0 (bt3.txt ubo=0): as before. */
#define UB_LOC 0x100000
#define UB_MAXM 32
typedef struct UMem { char name[32]; uint8_t blk; uint16_t off, stride, n; } UMem;
typedef struct Prog {
    GLint flip, u; float flipVal; uint32_t usize;
    uint32_t bsize[2];      /* the blocks' sizes (0: none) */
    uint8_t *shadow[2];     /* their contents as set */
    uint8_t dirty[2];
    uint32_t lim[2];        /* (R36_UbLimit) bytes the next copy needs, 0: all */
    long boff[2];           /* where they are (for sStreamGen) */
    GLuint bbuf[2];         /* in which buffer */
    unsigned bgen[2];
    int nm;
    UMem m[UB_MAXM];
} Prog;
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
typedef struct Shd { int cg, vertex, blocked; char *glsl, *plain; } Shd;
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
/* (the table and the ranges written: the drawing thread marks what it writes, the render thread hands it over) */
static pthread_mutex_t sBufLock = PTHREAD_MUTEX_INITIALIZER;
static Buf *buf_find_(GLuint id, int make) {
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
static Buf *buf_find(GLuint id, int make) {
    Buf *b;
    pthread_mutex_lock(&sBufLock);
    b = buf_find_(id, make);
    pthread_mutex_unlock(&sBufLock);
    return b;
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
    pthread_mutex_lock(&sCtxLock); /* (current on this thread now: held by it) */
    sCtxHeld = 1;
    pthread_mutex_unlock(&sCtxLock);
    tHold = 1;
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
    {   /* (TrimUI) the driver's costs measured once at start, into the log (r36s_bench.c): R36_BENCH=1 (bt3.txt bench=1) */
        const char *e = getenv("R36_BENCH");
        if (e != NULL && atoi(e) != 0) {
            extern void R36_Bench(void);
            R36_Bench();
        }
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
    {
        extern void R36_CrashHandlers(void);
        R36_CrashHandlers();
    }
    tHold = 1;
    sCtxHeld = 1;
    present_init();
    ctx_give(); /* (taken by whichever thread draws) */
    return 1;
}
void R36_ScreenSize(int *w, int *h) {
    *w = sWinW;
    *h = sWinH;
}

/* ---- textures ----
   The table of the renderer's texture numbers is made once at its full size (16384): the drawing thread takes a
   number for a texture the render thread will make (R36_TexNewLazy), and the table is never moved under the other. */
#define VTEX_CAP 16384
static pthread_mutex_t sTexLock = PTHREAD_MUTEX_INITIALIZER;
static int vtex_new(void) {
    int v;
    static int next = 1;
    pthread_mutex_lock(&sTexLock);
    if (sTex == NULL) {
        sTex = (VTex *)calloc(VTEX_CAP, sizeof(VTex));
        sTexCap = VTEX_CAP;
    }
    if (sTexFree >= 0) {
        v = sTexFree;
        sTexFree = (int)sTex[v].real;
    } else {
        if (next >= sTexCap) {
            pthread_mutex_unlock(&sTexLock);
            R36_LogLine("r36 gl: out of texture numbers\n");
            abort();
        }
        v = next++;
    }
    memset(&sTex[v], 0, sizeof(VTex));
    sTex[v].used = 1;
    pthread_mutex_unlock(&sTexLock);
    return v;
}
/* a texture number whose GL texture is made when it is first bound (by the thread that holds the context then) */
GLuint R36_TexNewLazy(void) {
    return (GLuint)vtex_new();
}
int gR36KeepDepth; /* (vita_vgl.c fxprev) the picture's depth stored at its end: read in the next picture */
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
    if (sTex[v].alias == 5) { /* (the picture before's) */
        return sDepthTexA[(gxm_back_buffer_index + NDISP - 1) % NDISP];
    }
    if (sTex[v].real == 0) { /* (R36_TexNewLazy's: made now) */
        p_glGenTextures(1, &sTex[v].real);
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
/* ---- the pool of textures let go ----
   The renderer makes some twenty textures a picture in a fight and lets as many go; on the PowerVR driver each
   glTexImage2D of a new one asks the kernel for memory (PVRSRVAllocSparseDevMem2, page faults), each delete gives it
   back. Here a texture let go is kept (up to POOL_N, POOL_MB) and given, three pictures later (the GPU done with
   it), to the next new texture of its size: its pixels then go in with glTexSubImage2D. R36_TEXPOOL=0: not. */
#define POOL_N 192
#define POOL_MB 48u
static struct { GLuint real; int w, h; GLint tp[4]; unsigned frame; } sPool[POOL_N];
static int sPoolN = -1;
static uint32_t sPoolBytes;
unsigned gR36Pool[3]; /* (for the log) taken from the pool, kept in it, made the usual way */
extern unsigned gR36Frames;
static int pool_on(void) {
    if (sPoolN < 0) {
        const char *e = getenv("R36_TEXPOOL");
        sPoolN = e != NULL && atoi(e) == 0 ? -2 : 0;
    }
    return sPoolN >= 0;
}
static int pool_take(int w, int h, VTex *t) {
    int k;
    for (k = 0; k < sPoolN; k++) {
        if (sPool[k].w == w && sPool[k].h == h && gR36Frames - sPool[k].frame >= 3u) {
            if (t->real != 0) {
                p_glDeleteTextures(1, &t->real); /* (the name made for it, with nothing in it yet) */
            }
            t->real = sPool[k].real;
            memcpy(t->tp, sPool[k].tp, sizeof(t->tp));
            sPoolBytes -= (uint32_t)(w * h * 4);
            sPool[k] = sPool[--sPoolN];
            return 1;
        }
    }
    return 0;
}
static int pool_keep(VTex *t) {
    uint32_t b = (uint32_t)(t->tw * t->th * 4);
    if (!pool_on() || t->tw <= 0 || t->real == 0 || t->alias != 0) {
        return 0;
    }
    if (sPoolN == POOL_N || sPoolBytes + b > (POOL_MB << 20)) { /* (full: the oldest let go) */
        int k, old = 0;
        for (k = 1; k < sPoolN; k++) {
            if ((int)(sPool[k].frame - sPool[old].frame) < 0) {
                old = k;
            }
        }
        if (sPoolN == 0) {
            return 0;
        }
        p_glDeleteTextures(1, &sPool[old].real);
        sPoolBytes -= (uint32_t)(sPool[old].w * sPool[old].h * 4);
        sPool[old] = sPool[--sPoolN];
        if (sPoolBytes + b > (POOL_MB << 20)) {
            return 0;
        }
    }
    sPool[sPoolN].real = t->real;
    sPool[sPoolN].w = t->tw;
    sPool[sPoolN].h = t->th;
    memcpy(sPool[sPoolN].tp, t->tp, sizeof(t->tp));
    sPool[sPoolN].frame = gR36Frames;
    sPoolN++;
    sPoolBytes += b;
    gR36Pool[1]++;
    return 1;
}
void r36_glDeleteTextures(GLsizei n, const GLuint *t) {
    ENTER();
    for (GLsizei i = 0; i < n; i++) {
        GLuint v = t[i];
        if (v == 0 || (int)v >= sTexCap || !sTex[v].used) {
            continue;
        }
        if (sTex[v].real != 0 && !pool_keep(&sTex[v])) {
            p_glDeleteTextures(1, &sTex[v].real);
        }
        for (int u = 0; u < 4; u++) {
            if (sBound[u] == v) {
                sBound[u] = 0;
            }
        }
        pthread_mutex_lock(&sTexLock);
        sTex[v].used = 0;
        sTex[v].alias = 0;
        sTex[v].real = (GLuint)sTexFree;
        sTexFree = (int)v;
        pthread_mutex_unlock(&sTexLock);
    }
    LEAVE();
}
void r36_glBindTexture(GLenum target, GLuint t) {
    ENTER();
    sBound[sUnit & 3] = t;
    p_glBindTexture(target, real_tex(t));
    gR36Chg[2]++;
    sEv |= 4;
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
    if (v != 0 && (int)v < sTexCap && target == GL_TEXTURE_2D && level == 0 && border == 0 && (ifmt == GL_RGBA || ifmt == GL_RGBA8) &&
        fmt == GL_RGBA && type == GL_UNSIGNED_BYTE && pool_on()) {
        VTex *tv = &sTex[v];
        if (tv->tw == w && tv->th == h) { /* (the same size again: its pixels only) */
            if (px != NULL) {
                p_glTexSubImage2D(target, 0, 0, 0, w, h, fmt, type, px);
            }
            sEv |= 8;
            LEAVE();
            return;
        }
        if (tv->tw == 0 && pool_take(w, h, tv)) { /* (a texture of that size let go three pictures ago) */
            p_glBindTexture(GL_TEXTURE_2D, tv->real);
            if (px != NULL) {
                p_glTexSubImage2D(target, 0, 0, 0, w, h, fmt, type, px);
            }
            tv->tw = w;
            tv->th = h;
            gR36Pool[0]++;
            sEv |= 8;
            LEAVE();
            return;
        }
        p_glTexImage2D(target, level, ifmt, w, h, border, fmt, type, px);
        tv->tw = w;
        tv->th = h;
        gR36Pool[2]++;
        sEv |= 8;
        LEAVE();
        return;
    }
    if (v != 0 && (int)v < sTexCap) {
        sTex[v].tw = 0; /* (another kind of texture: not for the pool) */
    }
    p_glTexImage2D(target, level, ifmt, w, h, border, fmt, type, px);
    sEv |= 8;
    LEAVE();
}
void r36_glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum type, const void *px) {
    ENTER();
    p_glTexSubImage2D(target, level, x, y, w, h, fmt, type, px);
    sEv |= 8;
    LEAVE();
}
void r36_glTexParameteri(GLenum target, GLenum pname, GLint param) {
    ENTER();
    GLuint v = sBound[sUnit & 3];
    int k = pname == GL_TEXTURE_MIN_FILTER ? 0 : pname == GL_TEXTURE_MAG_FILTER ? 1 : pname == GL_TEXTURE_WRAP_S ? 2 : pname == GL_TEXTURE_WRAP_T ? 3 : -1;
    if (v != 0 && (int)v < sTexCap && (sTex[v].alias == 4 || sTex[v].alias == 5) && (pname == GL_TEXTURE_MIN_FILTER || pname == GL_TEXTURE_MAG_FILTER)) {
        param = GL_NEAREST; /* (a depth texture is not filtered in OpenGL ES 3) */
    }
    if (target == GL_TEXTURE_2D && k >= 0 && v != 0 && (int)v < sTexCap && sTex[v].used && sTex[v].alias == 0) {
        if (gR36Opt && sTex[v].tp[k] == param) {
            LEAVE();
            return; /* (what it has already: the driver not told again) */
        }
        sTex[v].tp[k] = param;
    }
    sEv |= 64;
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
void vglTexImageDepthBufferPrev(GLenum target) { /* (the depth of the display buffer drawn before the one drawn now) */
    GLuint v = sBound[sUnit & 3];
    (void)target;
    if (v != 0 && (int)v < sTexCap) {
        ENTER();
        sTex[v].alias = 5;
        p_glBindTexture(GL_TEXTURE_2D, sDepthTexA[(gxm_back_buffer_index + NDISP - 1) % NDISP]);
        LEAVE();
    }
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
        p_glGenTextures(NDISP, sDepthTexA);
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
    for (i = 0; i < NDISP; i++) {
        p_glBindTexture(GL_TEXTURE_2D, sDepthTexA[i]);
        p_glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, w, h, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    for (i = 0; i < NDISP; i++) {
        GLenum st;
        p_glBindFramebuffer(GL_FRAMEBUFFER, sDispFbo[i]);
        p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sDispTex[i], 0);
        p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, sDepthTexA[i], 0);
        st = p_glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (st != GL_FRAMEBUFFER_COMPLETE) {
            R36_LogLine("r36 gl: display buffer %d not complete (%x)\n", i, (unsigned)st);
        }
    }
    sDispW = w;
    sDispH = h;
    rFbo = R_UNK;
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
    r_fbo(sOnDisp ? sDispFbo[gxm_back_buffer_index % NDISP] : sFboBound);
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
unsigned gR36DepthW; /* (vita_vgl.c fxprev) the display buffer's depth written or cleared: draws and clears that could */
void r36_glClear(GLbitfield mask) {
    ENTER();
    sEv |= 256;
    if (sOnDisp && (mask & GL_DEPTH_BUFFER_BIT)) {
        gR36DepthW++;
    }
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
    gR36Chg[5]++;
    sEv |= 16;
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
static int sUbOff = -1; /* R36_UBO=0 */
/* the GLSL with its uniforms (but samplers) moved into one block: NULL when there are none (or R36_UBO=0) */
static char *ub_block(const char *g, int vertex) {
    size_t n = strlen(g);
    char *out = (char *)malloc(n + 256), *mem = (char *)malloc(n + 64), *o = out, *m = mem;
    const char *line = g, *firstAt = NULL;
    int count = 0;
    if (sUbOff < 0) {
        const char *e = getenv("R36_UBO");
        sUbOff = e != NULL && atoi(e) == 0;
    }
    if (sUbOff) {
        free(out);
        free(mem);
        return NULL;
    }
    *m = 0;
    while (*line) {
        const char *end = strchr(line, '\n'), *t = line, *semi;
        size_t len = end != NULL ? (size_t)(end - line) + 1 : strlen(line);
        while (*t == ' ' || *t == '\t') {
            t++;
        }
        semi = (const char *)memchr(t, ';', len - (size_t)(t - line));
        if (strncmp(t, "uniform ", 8) == 0 && semi != NULL && !memchr(t, '{', len - (size_t)(t - line)) &&
            !memchr(t, '=', (size_t)(semi - t)) && !memchr(t, '(', (size_t)(semi - t))) {
            const char *a = t + 8;
            int sampler = 0;
            for (const char *q = a; q < semi; q++) {
                if (strncmp(q, "sampler", 7) == 0) {
                    sampler = 1;
                }
            }
            if (!sampler) {
                if (firstAt == NULL) {
                    firstAt = line;
                    o += sprintf(o, "@@BLOCK@@\n");
                }
                m += sprintf(m, "    %.*s;\n", (int)(semi - a), a);
                count++;
                line += len;
                continue;
            }
        }
        memcpy(o, line, len);
        o += len;
        line += len;
    }
    *o = 0;
    if (count == 0) {
        free(out);
        free(mem);
        return NULL;
    }
    {
        size_t need = strlen(out) + strlen(mem) + 128;
        char *r = (char *)malloc(need), *at = strstr(out, "@@BLOCK@@\n");
        snprintf(r, need, "%.*slayout(std140) uniform %s {\n%s};\n%s", (int)(at - out), out, vertex ? "R36V" : "R36F", mem, at + 10);
        free(out);
        free(mem);
        return r;
    }
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
            char *bl = ub_block(glsl, d->vertex);
            const char *g = bl != NULL ? bl : glsl;
            p_glShaderSource(s, 1, &g, NULL);
            free(d->glsl);
            free(d->plain);
            d->plain = bl != NULL ? glsl : NULL;
            d->glsl = bl != NULL ? bl : glsl;
            d->blocked = bl != NULL;
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
/* the program's blocks: bound to 0 and 1, their sizes, their members */
static void ub_setup(GLuint p, Prog *pr) {
    static const char *kB[2] = {"R36V", "R36F"};
    GLuint bi[2];
    GLint n = 0, b;
    if (p_glGetUniformBlockIndex == NULL || p_glBindBufferRange == NULL) {
        return;
    }
    for (b = 0; b < 2; b++) {
        GLint sz = 0;
        bi[b] = p_glGetUniformBlockIndex(p, kB[b]);
        if (bi[b] == GL_INVALID_INDEX) {
            continue;
        }
        p_glUniformBlockBinding(p, bi[b], (GLuint)b);
        p_glGetActiveUniformBlockiv(p, bi[b], GL_UNIFORM_BLOCK_DATA_SIZE, &sz);
        pr->bsize[b] = (uint32_t)sz;
        pr->shadow[b] = (uint8_t *)calloc(1, (size_t)sz + 16);
        pr->dirty[b] = 1;
    }
    p_glGetProgramiv(p, GL_ACTIVE_UNIFORMS, &n);
    for (GLint i = 0; i < n && pr->nm < UB_MAXM; i++) {
        char name[64];
        GLint size = 0, blk = -1, off = 0, st = 0;
        GLenum type = 0;
        GLuint ui = (GLuint)i;
        char *br;
        p_glGetActiveUniform(p, ui, sizeof(name), NULL, &size, &type, name);
        p_glGetActiveUniformsiv(p, 1, &ui, GL_UNIFORM_BLOCK_INDEX, &blk);
        if (blk < 0) {
            continue;
        }
        p_glGetActiveUniformsiv(p, 1, &ui, GL_UNIFORM_OFFSET, &off);
        p_glGetActiveUniformsiv(p, 1, &ui, GL_UNIFORM_ARRAY_STRIDE, &st);
        if ((br = strchr(name, '[')) != NULL) {
            *br = 0;
        }
        UMem *m = &pr->m[pr->nm++];
        snprintf(m->name, sizeof(m->name), "%s", name);
        m->blk = (uint8_t)((GLuint)blk == bi[1] && pr->bsize[1] != 0 ? 1 : 0);
        m->off = (uint16_t)off;
        m->stride = (uint16_t)st;
        m->n = (uint16_t)(size > 0 ? size : 1);
    }
}
/* a uniform's location: ours (UB_LOC + member << 12 + element) for a block's member */
static GLint ub_loc(GLuint p, const char *name) {
    Prog *pr = (int)p < sProgCap ? &sProg[p] : NULL;
    if (pr != NULL && pr->nm > 0) {
        char base[64];
        const char *br = strchr(name, '[');
        int el = br != NULL ? atoi(br + 1) : 0, k;
        snprintf(base, sizeof(base), "%.*s", br != NULL ? (int)(br - name) : (int)strlen(name), name);
        for (k = 0; k < pr->nm; k++) {
            if (strcmp(pr->m[k].name, base) == 0) {
                return el < pr->m[k].n ? UB_LOC + (k << 12) + el : -1;
            }
        }
    }
    return p_glGetUniformLocation(p, name);
}
GLint r36_glGetUniformLocation(GLuint p, const GLchar *name) {
    ENTER_R(-1);
    GLint r = ub_loc(p, name);
    LEAVE();
    return r;
}
/* a value into the current program's copy: 1 when the location is ours */
static int ub_set(GLint loc, const void *v, uint32_t bytes, int count) {
    Prog *pr;
    UMem *m;
    int el, i;
    if (loc < UB_LOC) {
        return 0;
    }
    pr = (int)sCurProg < sProgCap ? &sProg[sCurProg] : NULL;
    if (pr == NULL || ((loc - UB_LOC) >> 12) >= pr->nm) {
        return 1;
    }
    m = &pr->m[(loc - UB_LOC) >> 12];
    el = (loc - UB_LOC) & 0xFFF;
    for (i = 0; i < count && el + i < m->n; i++) {
        uint32_t at = m->off + (uint32_t)(el + i) * m->stride;
        if (at + bytes <= pr->bsize[m->blk]) {
            memcpy(pr->shadow[m->blk] + at, (const uint8_t *)v + (size_t)i * bytes, bytes);
        }
    }
    pr->dirty[m->blk] = 1;
    return 1;
}
void r36_glUniform1i(GLint loc, GLint v) { ENTER(); if (!ub_set(loc, &v, 4, 1)) { p_glUniform1i(loc, v); sEv |= 32; } LEAVE(); }
void r36_glUniform1f(GLint loc, GLfloat v) { ENTER(); if (!ub_set(loc, &v, 4, 1)) { p_glUniform1f(loc, v); sEv |= 32; } LEAVE(); }
void r36_glUniform4f(GLint loc, GLfloat a, GLfloat b, GLfloat c, GLfloat d) {
    ENTER();
    GLfloat v[4] = {a, b, c, d};
    if (!ub_set(loc, v, 16, 1)) {
        p_glUniform4f(loc, a, b, c, d);
        sEv |= 32;
    }
    LEAVE();
}
void r36_glUniform4fv(GLint loc, GLsizei count, const GLfloat *v) { ENTER(); if (!ub_set(loc, v, 16, count)) { p_glUniform4fv(loc, count, v); sEv |= 32; } LEAVE(); }
/* the next copy of the member's block need only reach its element el + count (the shader reads no further this
   draw): the rest of the block's members are copied still (the batched parts: their slots in use, not all 32) */
void R36_UbLimit(GLint loc, int count) {
    ENTER();
    Prog *pr = (int)sCurProg < sProgCap ? &sProg[sCurProg] : NULL;
    if (loc >= UB_LOC && pr != NULL && ((loc - UB_LOC) >> 12) < pr->nm) {
        int mi = (loc - UB_LOC) >> 12, k;
        UMem *m = &pr->m[mi];
        uint32_t end = m->off + (uint32_t)((loc - UB_LOC) & 0xFFF) * m->stride + (uint32_t)count * m->stride;
        for (k = 0; k < pr->nm; k++) {
            if (k != mi && pr->m[k].blk == m->blk) {
                uint32_t e = pr->m[k].off + (uint32_t)pr->m[k].n * pr->m[k].stride;
                end = e > end ? e : end;
            }
        }
        end = (end + 15u) & ~15u;
        pr->lim[m->blk] = end < pr->bsize[m->blk] ? end : 0;
    }
    LEAVE();
}
static void prog_free(Prog *pr) {
    free(pr->shadow[0]);
    free(pr->shadow[1]);
}
void r36_glLinkProgram(GLuint p) {
    ENTER();
    GLint ok = 0, n = 0;
    Prog *pr;
    p_glLinkProgram(p);
    p_glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok && p_glGetAttachedShaders != NULL) { /* (the blocks the reason, say a name in both stages: the shaders as they were) */
        GLuint sh[4];
        GLsizei ns = 0, i, redo = 0;
        p_glGetAttachedShaders(p, 4, &ns, sh);
        for (i = 0; i < ns; i++) {
            Shd *d = shd_of(sh[i]);
            if (d->blocked && d->plain != NULL) {
                const char *g = d->plain;
                p_glShaderSource(sh[i], 1, &g, NULL);
                p_glCompileShader(sh[i]);
                free(d->glsl);
                d->glsl = d->plain;
                d->plain = NULL;
                d->blocked = 0;
                redo = 1;
            }
        }
        if (redo) {
            R36_LogLine("r36 gl: program %u linked without uniform blocks\n", (unsigned)p);
            p_glLinkProgram(p);
            p_glGetProgramiv(p, GL_LINK_STATUS, &ok);
        }
    }
    pr = prog_of(p);
    prog_free(pr);
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
    ub_setup(p, pr);
    pr->flip = ub_loc(p, "r36_flip");
    pr->u = ub_loc(p, "U");
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
    if ((pr->bsize[0] | pr->bsize[1]) != 0) {
        static int logged;
        if (logged++ < 3) {
            R36_LogLine("r36 gl: program %u: uniform blocks of %u and %u bytes, %d members\n", (unsigned)p, pr->bsize[0], pr->bsize[1], pr->nm);
        }
    }
    LEAVE();
}
void r36_glUseProgram(GLuint p) {
    ENTER();
    sCurProg = p;
    r_use(p);
    LEAVE();
}

/* ---- buffers ---- */
void r36_glBindBuffer(GLenum target, GLuint b) {
    ENTER();
    if (target == GL_ARRAY_BUFFER) {
        sArrayBuf = b;
    } else if (target == GL_ELEMENT_ARRAY_BUFFER) {
        sElemBuf = b;
        if (gR36Opt) { /* (bound at the draw that uses it: every draw binds the one it needs; the renderer's binds
                          of 0 between draws were two calls into the driver a run) */
            LEAVE();
            return;
        }
    }
    r_bind(target, b);
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
    if (target == GL_ELEMENT_ARRAY_BUFFER) {
        r_bind(target, sElemBuf); /* (vglFastDrawPtr leaves 0 bound) */
    } else if (target == GL_ARRAY_BUFFER) {
        r_arr_sync();
    }
    p_glBufferData(target, size, data, usage);
    LEAVE();
}
void r36_glDeleteBuffers(GLsizei n, const GLuint *b) {
    ENTER();
    pthread_mutex_lock(&sBufLock);
    for (GLsizei i = 0; i < n; i++) {
        Buf *bf = buf_find_(b[i], 0);
        if (bf != NULL) {
            free(bf->shadow);
            *bf = sBufs[--sNBufs];
        }
    }
    pthread_mutex_unlock(&sBufLock);
    r_arr_sync();
    for (GLsizei i = 0; i < n; i++) {
        if (b[i] == rArrWant) {
            rArrWant = 0; /* (a bound buffer let go: 0 bound in its place) */
        }
    }
    p_glDeleteBuffers(n, b);
    r_forget(); /* (a buffer bound or under an attribute is let go of: 0 in its place; its name may come back) */
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
static void dirty_(const uint8_t *p, size_t size);
void vglR36Dirty(const void *at, size_t size) {
    if (size == 0) {
        return;
    }
    pthread_mutex_lock(&sBufLock);
    dirty_((const uint8_t *)at, size);
    pthread_mutex_unlock(&sBufLock);
}
static void dirty_(const uint8_t *p, size_t size) {
    int i;
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
unsigned gR36Up[3];       /* (for the log) bytes and ranges given to the GPU from mapped buffers */
static int sNoUnsync = -1;
static void flush_dirty(void) {
    int i, k, nb = 0;
    struct { GLuint id; uint8_t *shadow; int nr; struct { uint32_t lo, hi; } r[32]; } w[16];
    if (!sDirty) {
        return;
    }
    if (sNoUnsync < 0) {
        const char *e = getenv("R36_NOUNSYNC");
        sNoUnsync = e != NULL && atoi(e) != 0;
    }
    pthread_mutex_lock(&sBufLock); /* (the ranges taken as they are now; what is marked after waits for the next draw) */
    sDirty = 0;
    for (i = 0; i < sNBufs; i++) {
        if (sBufs[i].nr != 0 && sBufs[i].shadow != NULL) {
            w[nb].id = sBufs[i].id;
            w[nb].shadow = sBufs[i].shadow;
            w[nb].nr = sBufs[i].nr;
            for (k = 0; k < sBufs[i].nr; k++) {
                w[nb].r[k].lo = sBufs[i].r[k].lo;
                w[nb].r[k].hi = sBufs[i].r[k].hi;
            }
            nb++;
        }
        sBufs[i].nr = 0;
    }
    pthread_mutex_unlock(&sBufLock);
    for (i = 0; i < nb; i++) {
        typeof(&w[0]) b = &w[i];
        p_glBindBuffer(GL_COPY_WRITE_BUFFER, b->id);
        for (k = 0; k < b->nr; k++) {
            /* Written without waiting for the GPU and without the driver copying the buffer for it (glBufferSubData
               on a buffer a queued draw still reads: the Mali driver copied all 16 MB of the stage's): the renderer
               only writes places nothing drawn yet has used (vitaGL's memory was written the same way). */
            GLsizeiptr len = (GLsizeiptr)(b->r[k].hi - b->r[k].lo);
            void *dst = sNoUnsync ? NULL : p_glMapBufferRange(GL_COPY_WRITE_BUFFER, (GLintptr)b->r[k].lo, len,
                                                             GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_INVALIDATE_RANGE_BIT);
            if (dst != NULL) {
                memcpy(dst, b->shadow + b->r[k].lo, (size_t)len);
                p_glUnmapBuffer(GL_COPY_WRITE_BUFFER);
            } else {
                p_glBufferSubData(GL_COPY_WRITE_BUFFER, (GLintptr)b->r[k].lo, len, b->shadow + b->r[k].lo);
            }
            gR36Up[0] += (unsigned)len;
            gR36Up[1]++;
        }
    }
    p_glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
}

/* ---- the stream: vertices and indices written once for one draw ----
   The renderer's batches of GS primitives gave their vertices to the driver with glBufferData on one buffer, draw
   after draw: on the TrimUI's PowerVR driver a costly call (a new store for the buffer each time, the old one kept
   while the GPU reads it). Here they are copied into one large buffer mapped once for good (EXT_buffer_storage,
   persistent and coherent; else mapped per piece without waiting), in three parts used a picture each in turn: a
   part is written again only after the GPU has finished the picture that used it (a fence at each swap).
   R36_STREAM=0: as before. */
#define STREAM_PART (6u << 20)
static GLuint sStreamBuf;
static uint8_t *sStreamMap;   /* (persistent mapping, or NULL: mapped per piece) */
static int sStreamOff = -1;   /* -1 not tried yet, 1 off */
static unsigned sStreamPart, sStreamAt;
static GLsync sStreamFence[3];
static unsigned sStreamGen = 1; /* (counts the parts begun: what was put in the stream before may be written over) */
static void stream_init(void) {
    const char *e = getenv("R36_STREAM");
    sStreamOff = e != NULL && atoi(e) == 0;
    if (sStreamOff || p_glFenceSync == NULL || p_glClientWaitSync == NULL) {
        sStreamOff = 1;
        return;
    }
    p_glGenBuffers(1, &sStreamBuf);
    p_glBindBuffer(GL_COPY_WRITE_BUFFER, sStreamBuf);
    if (p_glBufferStorageEXT != NULL) {
        p_glBufferStorageEXT(GL_COPY_WRITE_BUFFER, STREAM_PART * 3, NULL, GL_MAP_WRITE_BIT | 0x0040 /* PERSISTENT */ | 0x0080 /* COHERENT */);
        if (p_glGetError() == GL_NO_ERROR) {
            sStreamMap = (uint8_t *)p_glMapBufferRange(GL_COPY_WRITE_BUFFER, 0, STREAM_PART * 3, GL_MAP_WRITE_BIT | 0x0040 | 0x0080);
        }
    }
    if (sStreamMap == NULL) {
        p_glGetError();
        p_glBufferData(GL_COPY_WRITE_BUFFER, STREAM_PART * 3, NULL, GL_DYNAMIC_DRAW);
    }
    p_glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    R36_LogLine("r36 gl: the stream: %u MB in 3 parts, %s\n", (STREAM_PART * 3) >> 20, sStreamMap != NULL ? "mapped for good" : "mapped per piece");
}
/* (at a swap) this part done: a fence after it; the next one waited for if the GPU still reads it */
static void stream_next(void) {
    if (sStreamOff != 0 || sStreamBuf == 0) {
        return;
    }
    if (sStreamFence[sStreamPart] != NULL) {
        p_glDeleteSync(sStreamFence[sStreamPart]);
    }
    sStreamFence[sStreamPart] = p_glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    sStreamPart = (sStreamPart + 1) % 3;
    sStreamAt = 0;
    sStreamGen++;
    if (sStreamFence[sStreamPart] != NULL) {
        p_glClientWaitSync(sStreamFence[sStreamPart], GL_SYNC_FLUSH_COMMANDS_BIT, 100000000ull);
        p_glDeleteSync(sStreamFence[sStreamPart]);
        sStreamFence[sStreamPart] = NULL;
    }
}
/* copies n bytes into the stream: its offset in the stream buffer (R36_StreamBuf), or -1 (no room: the caller's
   own way) */
GLuint R36_StreamBuf(void) { return sStreamBuf; }
static long stream_put(const void *data, size_t n, unsigned align);
long R36_Stream(const void *data, size_t n) {
    long at;
    ENTER_R(-1);
    at = stream_put(data, n, 16);
    LEAVE();
    return at;
}
static long stream_put(const void *data, size_t n, unsigned align) {
    long at;
    if (sStreamOff < 0) {
        stream_init();
    }
    if (sStreamOff == 0) {
        sStreamAt = (sStreamAt + align - 1u) / align * align;
    }
    if (sStreamOff != 0 || n == 0 || sStreamAt + n + 16 > STREAM_PART) {
        return -1;
    }
    at = (long)(sStreamPart * STREAM_PART + sStreamAt);
    if (sStreamMap != NULL) {
        memcpy(sStreamMap + at, data, n);
    } else {
        void *dst;
        p_glBindBuffer(GL_COPY_WRITE_BUFFER, sStreamBuf);
        dst = p_glMapBufferRange(GL_COPY_WRITE_BUFFER, at, (GLsizeiptr)n, GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_INVALIDATE_RANGE_BIT);
        if (dst != NULL) {
            memcpy(dst, data, n);
            p_glUnmapBuffer(GL_COPY_WRITE_BUFFER);
        } else {
            p_glBufferSubData(GL_COPY_WRITE_BUFFER, at, (GLsizeiptr)n, data);
        }
        p_glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    }
    sStreamAt = (sStreamAt + (unsigned)n + 15u) & ~15u;
    gR36Up[2] += (unsigned)n;
    return at;
}
/* (before a draw) the current program's blocks: a changed copy put where the GPU reads it, its range bound.
   Where (gR36UbMode; bt3.txt ubmode, ubab: every 10 s the next one, prof.txt says which):
   1 the stream (persistent, coherent, also vertices and indices), 2 a buffer a part of its own written with
   glBufferSubData, 3 a persistent coherent buffer of its own, 4 one small buffer per block given the copy with
   glBufferData each time. */
int gR36UbMode = 3;
static struct { GLuint buf; long off; uint32_t size; } sUbBound[2] = {{0, -1, 0}, {0, -1, 0}};
static GLuint sUbFb[2];
static unsigned sUbAlign;
unsigned gR36Ub[3]; /* (for the log) blocks put in place, ranges bound, bytes */
#define UB_PART (1u << 20)
static GLuint sUbRing2[3], sUbRing3;
static uint8_t *sUbMap3;
static unsigned sUbAt, sUbGen; /* (modes 2, 3: the place in this part; the part's generation as sStreamGen) */
int gR36UbLim = 1; /* R36_UbLimit followed (bt3.txt ublim) */
static void ub_bind(Prog *p) {
    int b, mode = gR36UbMode;
    if (sUbAlign == 0) {
        GLint a = 0;
        p_glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &a);
        sUbAlign = a > 16 ? (unsigned)a : 16u;
        R36_LogLine("r36 gl: uniform buffers aligned to %u bytes\n", sUbAlign);
    }
    if (sUbGen != sStreamGen) {
        sUbGen = sStreamGen;
        sUbAt = 0;
    }
    if (mode == 3 && sUbRing3 == 0 && p_glBufferStorageEXT != NULL) {
        p_glGenBuffers(1, &sUbRing3);
        p_glBindBuffer(GL_UNIFORM_BUFFER, sUbRing3);
        p_glBufferStorageEXT(GL_UNIFORM_BUFFER, UB_PART * 3, NULL, GL_MAP_WRITE_BIT | 0x0040 | 0x0080);
        sUbMap3 = (uint8_t *)p_glMapBufferRange(GL_UNIFORM_BUFFER, 0, UB_PART * 3, GL_MAP_WRITE_BIT | 0x0040 | 0x0080);
        p_glBindBuffer(GL_UNIFORM_BUFFER, 0);
        R36_LogLine("r36 gl: uniform ring (mode 3) %s\n", sUbMap3 != NULL ? "mapped" : "NOT mapped");
    }
    if (mode == 3 && sUbMap3 == NULL) {
        mode = 2;
    }
    for (b = 0; b < 2; b++) {
        uint32_t sz = p->bsize[b];
        GLuint buf = 0;
        if (sz == 0) {
            continue;
        }
        if (p->dirty[b] || p->bgen[b] != sStreamGen * 8u + (unsigned)mode || p->boff[b] < 0) {
            long at = -1;
            uint32_t cs = mode == 3 && p->lim[b] != 0 && gR36UbLim ? p->lim[b] : sz; /* (the bytes copied: the range bound is the whole block still) */
            unsigned part = sStreamPart;
            if (mode == 1) {
                at = stream_put(p->shadow[b], sz, sUbAlign);
            } else if (mode == 2 || mode == 3) {
                unsigned o = (sUbAt + sUbAlign - 1u) / sUbAlign * sUbAlign;
                if (o + sz <= UB_PART) {
                    sUbAt = o + cs;
                    if (mode == 2) {
                        if (sUbRing2[part] == 0) {
                            p_glGenBuffers(1, &sUbRing2[part]);
                            p_glBindBuffer(GL_UNIFORM_BUFFER, sUbRing2[part]);
                            p_glBufferData(GL_UNIFORM_BUFFER, UB_PART, NULL, GL_DYNAMIC_DRAW);
                        } else {
                            p_glBindBuffer(GL_UNIFORM_BUFFER, sUbRing2[part]);
                        }
                        p_glBufferSubData(GL_UNIFORM_BUFFER, o, sz, p->shadow[b]);
                        p_glBindBuffer(GL_UNIFORM_BUFFER, 0);
                        at = (long)o;
                    } else {
                        at = (long)(part * UB_PART + o);
                        memcpy(sUbMap3 + at, p->shadow[b], cs);
                    }
                }
            }
            p->dirty[b] = 0;
            p->bgen[b] = sStreamGen * 8u + (unsigned)mode;
            p->boff[b] = at;
            p->bbuf[b] = mode == 1 ? sStreamBuf : mode == 2 ? sUbRing2[part] : sUbRing3;
            gR36Ub[0]++;
            gR36Ub[2] += cs;
            p->lim[b] = 0;
            if (at < 0) { /* (mode 4, or no room: a buffer of its own given the copy) */
                if (sUbFb[b] == 0) {
                    p_glGenBuffers(1, &sUbFb[b]);
                }
                p_glBindBuffer(GL_UNIFORM_BUFFER, sUbFb[b]);
                p_glBufferData(GL_UNIFORM_BUFFER, sz, p->shadow[b], GL_STREAM_DRAW);
                p_glBindBuffer(GL_UNIFORM_BUFFER, 0);
                p_glBindBufferRange(GL_UNIFORM_BUFFER, (GLuint)b, sUbFb[b], 0, sz);
                sUbBound[b].off = -1;
                p->dirty[b] = 1; /* (next time again) */
                gR36Ub[1]++;
                continue;
            }
        }
        buf = p->bbuf[b];
        if (sUbBound[b].buf != buf || sUbBound[b].off != p->boff[b] || sUbBound[b].size != sz) {
            p_glBindBufferRange(GL_UNIFORM_BUFFER, (GLuint)b, buf, p->boff[b], sz);
            sUbBound[b].buf = buf;
            sUbBound[b].off = p->boff[b];
            sUbBound[b].size = sz;
            gR36Ub[1]++;
        }
    }
}
GLuint R36_ElemBuf(void) { return sElemBuf; }
/* (as R36_Stream) at a multiple of 32 bytes: a vertex's place in the stream as a whole number of vertices */
long R36_Stream32(const void *data, size_t n) {
    if (sStreamOff == 0) {
        sStreamAt = (sStreamAt + 31u) & ~31u;
    }
    return R36_Stream(data, n);
}

/* ---- vertex attributes ---- */
void r36_glEnableVertexAttribArray(GLuint i) {
    ENTER();
    if (i < 4) { sAttr[i].on = 1; }
    r_attr_on(i, 1);
    LEAVE();
}
void r36_glDisableVertexAttribArray(GLuint i) {
    ENTER();
    if (i < 4) { sAttr[i].on = 0; }
    r_attr_on(i, 0);
    LEAVE();
}
void r36_glVertexAttribPointer(GLuint i, GLint size, GLenum type, GLboolean norm, GLsizei stride, const void *ptr) {
    ENTER();
    if (i < 4) {
        Attr *a = &sAttr[i];
        a->set = 1; a->size = size; a->type = type; a->norm = norm; a->stride = stride; a->ptr = ptr; a->buf = sArrayBuf;
    }
    r_vap(i, size, type, norm, stride, ptr);
    LEAVE();
}
static void attr_restore(void) { /* the renderer's attributes 0..3 back as it set them */
    int i;
    for (i = 0; i < 4; i++) {
        Attr *a = &sAttr[i];
        if (a->set) {
            r_bind(GL_ARRAY_BUFFER, a->buf);
            r_vap((GLuint)i, a->size, a->type, a->norm, a->stride, a->ptr);
        }
        r_attr_on((GLuint)i, a->on);
    }
    r_bind(GL_ARRAY_BUFFER, sArrayBuf);
}

/* ---- draws ---- */
unsigned gR36Draws;
__attribute__((weak)) unsigned gR36Dpb[3]; /* (vita_vgl.c's; the tests have none) */
/* (for prof.txt) the draws by the place in the renderer that asked for them: a return address and its count, and
   the time the driver took for them */
#define DSITE 64
unsigned gR36SiteA[DSITE], gR36SiteN[DSITE];
unsigned long long gR36SiteNs[DSITE];
static __thread uintptr_t tSite;
static inline void site_note(uintptr_t a) { tSite = a; }
static void site_add(unsigned long long ns) {
    uint32_t a = (uint32_t)tSite, h = (a * 2654435761u) >> 26, k;
    for (k = 0; k < DSITE; k++) {
        unsigned j = (h + k) & (DSITE - 1);
        if (gR36SiteA[j] == a || gR36SiteA[j] == 0) {
            gR36SiteA[j] = a;
            gR36SiteN[j]++;
            gR36SiteNs[j] += ns;
            return;
        }
    }
}
/* (the draws timed only for prof.txt: R36_PROF=0, the release launcher, leaves out two clock reads a draw) */
static int sDrawTimed = -1;
static inline int draw_timed(void) {
    if (sDrawTimed < 0) {
        const char *e = getenv("R36_PROF");
        sDrawTimed = !(e != NULL && atoi(e) == 0);
    }
    return sDrawTimed;
}
static unsigned long long draw_t0(void) { return draw_timed() ? glt_now() : 0; }
static void draw_done(unsigned long long t0) {
    if (!sDrawTimed) {
        sEv = 0;
        return;
    }
    unsigned long long d = glt_now() - t0;
    site_add(d);
    unsigned e = sEv & (EV_N - 1);
    sAllN[e]++;
    sAllNs[e] += d;
    if (d > 1000000ull) {
        sSlowN[e]++;
        sSlowNs[e] += d;
    }
    sEv = 0;
}
static GLboolean sDepthW = GL_TRUE;
static void pre_draw(void) {
    gR36Draws++;
    if (sOnDisp && sCapDepth && sDepthW) {
        gR36DepthW++;
    }
    flush_dirty();
    if (sCurProg != 0 && (int)sCurProg < sProgCap) {
        Prog *p = &sProg[sCurProg];
        float f = sOnDisp ? -1.0f : 1.0f;
        if (p->flip >= 0 && p->flipVal != f) {
            if (!ub_set(p->flip, &f, 4, 1)) {
                p_glUniform1f(p->flip, f);
            }
            p->flipVal = f;
        }
        if ((p->bsize[0] | p->bsize[1]) != 0) {
            ub_bind(p);
        }
    }
}
/* a draw with program 0: the fixed-function pipeline's way, with our shader */
static void ff_attr(int i, const Attr *a, int on) {
    if (on && a->ptr != NULL) {
        r_bind(GL_ARRAY_BUFFER, 0);
        r_vap((GLuint)i, a->size, a->type, a->type == GL_UNSIGNED_BYTE ? GL_TRUE : GL_FALSE, a->stride, a->ptr);
        r_attr_on((GLuint)i, 1);
    } else {
        r_attr_on((GLuint)i, 0);
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
    r_use(sFFProg);
    p_glUniform1f(sFFU[0], sOnDisp ? -1.0f : 1.0f);
    p_glUniform4f(sFFU[1], sFF.color[0], sFF.color[1], sFF.color[2], sFF.color[3]);
    p_glUniform1f(sFFU[2], sFF.ca ? 1.0f : 0.0f);
    p_glUniform1i(sFFU[3], 0);
    p_glUniform4f(sFFU[4], kEnv[env], (float)af, sFF.aref, 0.0f);
    ff_attr(0, &sFF.v, sFF.va);
    ff_attr(1, &sFF.c, sFF.ca);
    ff_attr(2, &sFF.t, sFF.ta);
    r_attr_on(3, 0);
}
static void ff_end(void) {
    r_use(sCurProg);
    attr_restore();
}
void r36_glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    ENTER();
    site_note((uintptr_t)__builtin_return_address(0));
    pre_draw();
    if (sCurProg == 0) {
        ff_begin();
        { unsigned long long dt0_ = draw_t0(); p_glDrawArrays(mode, first, count); draw_done(dt0_); }
        ff_end();
    } else {
        { unsigned long long dt0_ = draw_t0(); p_glDrawArrays(mode, first, count); draw_done(dt0_); }
    }
    LEAVE();
}
void r36_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *idx) {
    ENTER();
    site_note((uintptr_t)__builtin_return_address(0));
    pre_draw();
    if (sCurProg == 0) {
        ff_begin();
        r_bind(GL_ELEMENT_ARRAY_BUFFER, sElemBuf);
        { unsigned long long dt0_ = draw_t0(); p_glDrawElements(mode, count, type, idx); draw_done(dt0_); }
        ff_end();
    } else {
        r_bind(GL_ELEMENT_ARRAY_BUFFER, sElemBuf);
        { unsigned long long dt0_ = draw_t0(); p_glDrawElements(mode, count, type, idx); draw_done(dt0_); }
    }
    LEAVE();
}
/* (vita_vgl.c) the bound target's colour not needed (all of it is drawn next): the GPU does not load it. R36_DISCARD=0: not */
static int sDiscardOff = -1;
int gR36DiscardAb = 1; /* (vita_vgl.c's A/B: 0 not now) */
static int discard_on(void) {
    if (sDiscardOff < 0) {
        const char *e = getenv("R36_DISCARD");
        sDiscardOff = e != NULL && atoi(e) == 0;
    }
    return !sDiscardOff && gR36DiscardAb && p_glInvalidateFramebuffer != NULL;
}
void R36_DiscardColor(void) {
    static const GLenum kC[1] = {GL_COLOR_ATTACHMENT0};
    if (!discard_on()) {
        return;
    }
    ENTER();
    p_glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, kC);
    LEAVE();
}
/* the clear values and write masks as the renderer set them (R36_ClearRect puts them back) */
static GLfloat sClrC[4] = {0, 0, 0, 1}, sClrD = 0.0f;
static GLint sClrS;
static GLuint sStenW = 0xFFFFFFFFu;
void r36_glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { ENTER(); sClrC[0] = r; sClrC[1] = g; sClrC[2] = b; sClrC[3] = a; p_glClearColor(r, g, b, a); LEAVE(); }
void r36_glClearDepthf(GLfloat d) { ENTER(); sClrD = d; p_glClearDepthf(d); LEAVE(); }
void r36_glClearStencil(GLint v) { ENTER(); sClrS = v; p_glClearStencil(v); LEAVE(); }
void r36_glDepthMask(GLboolean f) { ENTER(); sDepthW = f; p_glDepthMask(f); sEv |= 16; LEAVE(); }
void r36_glStencilMask(GLuint m) { ENTER(); sStenW = m; p_glStencilMask(m); sEv |= 16; LEAVE(); }
/* (vita_vgl.c) a rectangle in the current viewport's units (-1..1) cleared instead of drawn: the alpha to 0
   (alpha != 0), the depth to dval (depth != 0), stencil bit 0 to sval (sten != 0); inside the scissor as it is. On a
   tile-based GPU a clear costs nothing per pixel; the GS sprites that clear the picture's alpha (two to eight
   screens a picture) were drawn pixel by pixel. */
void R36_ClearRect(float x0, float y0, float x1, float y1, int alpha, int depth, float dval, int sten, int sval) {
    float fx0 = (float)sVp[0] + (x0 * 0.5f + 0.5f) * (float)sVp[2], fx1 = (float)sVp[0] + (x1 * 0.5f + 0.5f) * (float)sVp[2];
    float fy0 = (float)sVp[1] + (y0 * 0.5f + 0.5f) * (float)sVp[3], fy1 = (float)sVp[1] + (y1 * 0.5f + 0.5f) * (float)sVp[3];
    int ix0, ix1, iy0, iy1;
    GLbitfield bits = 0;
    if (fx0 > fx1) { float t = fx0; fx0 = fx1; fx1 = t; }
    if (fy0 > fy1) { float t = fy0; fy0 = fy1; fy1 = t; }
    /* (the pixels whose centres are inside, as the triangles would have drawn) */
    ix0 = (int)ceilf(fx0 - 0.5f); ix1 = (int)ceilf(fx1 - 0.5f);
    iy0 = (int)ceilf(fy0 - 0.5f); iy1 = (int)ceilf(fy1 - 0.5f);
    if (sScOn) {
        if (ix0 < sSc[0]) { ix0 = sSc[0]; }
        if (iy0 < sSc[1]) { iy0 = sSc[1]; }
        if (ix1 > sSc[0] + sSc[2]) { ix1 = sSc[0] + sSc[2]; }
        if (iy1 > sSc[1] + sSc[3]) { iy1 = sSc[1] + sSc[3]; }
    }
    if (ix1 <= ix0 || iy1 <= iy0) {
        return;
    }
    ENTER();
    p_glEnable(GL_SCISSOR_TEST);
    p_glScissor(ix0, sOnDisp ? sDispH - iy1 : iy0, ix1 - ix0, iy1 - iy0);
    if (alpha) {
        p_glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
        p_glClearColor(0, 0, 0, 0);
        bits |= GL_COLOR_BUFFER_BIT;
    }
    if (depth) {
        if (sOnDisp) {
            gR36DepthW++;
        }
        p_glDepthMask(GL_TRUE);
        p_glClearDepthf(dval);
        bits |= GL_DEPTH_BUFFER_BIT;
    }
    if (sten) {
        p_glStencilMask(1);
        p_glClearStencil(sval);
        bits |= GL_STENCIL_BUFFER_BIT;
    }
    p_glClear(bits);
    /* (all of it back as it was) */
    if (sScOn) {
        p_glScissor(sSc[0], sOnDisp ? sDispH - sSc[1] - sSc[3] : sSc[1], sSc[2], sSc[3]);
    } else {
        p_glDisable(GL_SCISSOR_TEST);
    }
    if (alpha) {
        p_glColorMask(sMask[0], sMask[1], sMask[2], sMask[3]);
        p_glClearColor(sClrC[0], sClrC[1], sClrC[2], sClrC[3]);
    }
    if (depth) {
        p_glDepthMask(sDepthW);
        p_glClearDepthf(sClrD);
    }
    if (sten) {
        p_glStencilMask(sStenW);
        p_glClearStencil(sClrS);
    }
    sEv |= 256;
    LEAVE();
}
/* (vita_vgl.c) indices in the stream at off, their vertices from vertex base on: 0 when the driver has no such draw */
int R36_DrawElementsBase(GLenum mode, GLsizei count, GLenum type, long off, GLint base) {
    if (p_glDrawElementsBaseVertex == NULL || sCurProg == 0) {
        return 0;
    }
    ENTER_R(0);
    site_note((uintptr_t)__builtin_return_address(0));
    pre_draw();
    r_bind(GL_ELEMENT_ARRAY_BUFFER, sStreamBuf);
    { unsigned long long dt0_ = draw_t0(); p_glDrawElementsBaseVertex(mode, count, type, (const void *)(uintptr_t)off, base); draw_done(dt0_); }
    if (!gR36Opt) {
        r_bind(GL_ELEMENT_ARRAY_BUFFER, sElemBuf);
    }
    LEAVE();
    return 1;
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
    site_note((uintptr_t)__builtin_return_address(0));
    Prog *p = (int)sCurProg < sProgCap ? &sProg[sCurProg] : NULL;
    if (p != NULL && p->u >= 0) {
        if (!ub_set(p->u, vunif, 16, (int)(size / 16u))) {
            p_glUniform4fv(p->u, (GLsizei)(size / 16u), (const GLfloat *)vunif);
        }
        gR36Chg[4] += size;
    }
    pre_draw();
    r_bind(GL_ELEMENT_ARRAY_BUFFER, sElemBuf);
    { unsigned long long dt0_ = draw_t0(); p_glDrawElements(GL_TRIANGLES, (GLsizei)count, type, (const void *)(uintptr_t)offset); draw_done(dt0_); }
    LEAVE();
}
void vglFastDrawPtr(GLenum type, const void *idx, uint32_t count, const void *vunif, uint32_t size) {
    ENTER();
    site_note((uintptr_t)__builtin_return_address(0));
    Prog *p = (int)sCurProg < sProgCap ? &sProg[sCurProg] : NULL;
    if (p != NULL && p->u >= 0) {
        if (!ub_set(p->u, vunif, 16, (int)(size / 16u))) {
            p_glUniform4fv(p->u, (GLsizei)(size / 16u), (const GLfloat *)vunif);
        }
        gR36Chg[4] += size;
    }
    pre_draw();
    {   /* (the indices into the stream, else from main memory; the renderer's own put back at the next draw that needs it) */
        long at = R36_Stream(idx, (size_t)count * (type == GL_UNSIGNED_INT ? 4u : type == GL_UNSIGNED_BYTE ? 1u : 2u));
        if (at >= 0) {
            r_bind(GL_ELEMENT_ARRAY_BUFFER, sStreamBuf);
            { unsigned long long dt0_ = draw_t0(); p_glDrawElements(GL_TRIANGLES, (GLsizei)count, type, (const void *)(uintptr_t)at); draw_done(dt0_); }
        } else {
            r_bind(GL_ELEMENT_ARRAY_BUFFER, 0);
            { unsigned long long dt0_ = draw_t0(); p_glDrawElements(GL_TRIANGLES, (GLsizei)count, type, idx); draw_done(dt0_); }
        }
    }
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
/* (vita_vgl.c, every few ms of a fight's picture) the picture so far handed to the GPU now (glFlush: the driver
   renders what it has), so that a read of the picture later in it (the fog's depth, the ids' copy) waits only for
   what came after: each such read made this thread wait ~0.8 ms and more for all the GPU had not drawn yet
   (r36s_bench.c: "away+back, reading its colour"). gR36Split 0 (bt3.txt split=0): not. */
int gR36Split = 1;
unsigned gR36SplitN;
GLboolean vglSplitScene(void) {
    if (!gR36Split) {
        return GL_FALSE;
    }
    ENTER_R(GL_FALSE);
    p_glFlush();
    gR36SplitN++;
    LEAVE();
    return GL_TRUE;
}
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
    tSticky = 1; /* (the thread that starts the renderer draws: it keeps the context between its calls) */
    if (!tHold) {
        ctx_take();
    }
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
    {
        extern void R36_CrashHandlers(void);
        R36_CrashHandlers();
    }
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
    r_fbo(0);
    p_glViewport(0, 0, sWinW, sWinH);
    p_glDisable(GL_SCISSOR_TEST);
    p_glDisable(GL_BLEND);
    p_glDisable(GL_DEPTH_TEST);
    p_glDisable(GL_STENCIL_TEST);
    p_glDisable(GL_CULL_FACE);
    p_glColorMask(1, 1, 1, 1);
    p_glClearColor(0, 0, 0, 1);
    p_glClear(GL_COLOR_BUFFER_BIT);
    r_use(sPresentProg);
    p_glUniform4f(sPresentU[0], sw, flipped ? sh : -sh, 0.0f, 0.0f);
    p_glUniform1i(sPresentU[1], 0);
    p_glActiveTexture(GL_TEXTURE0);
    p_glBindTexture(GL_TEXTURE_2D, tex);
    r_bind(GL_ARRAY_BUFFER, 0);
    r_vap(0, 2, GL_FLOAT, GL_FALSE, 0, kQuad);
    r_attr_on(0, 1);
    r_attr_on(1, 0);
    r_attr_on(2, 0);
    r_attr_on(3, 0);
    p_glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    SDL_GL_SwapWindow(sWin);
}
/* ---- the present thread ----
   Putting a finished picture on the screen (drawing it onto the window, the swap, which waits for the GPU and for the
   display's blank) took the render thread 2 to 15 ms a picture, every picture, while nothing else was drawn. Here a
   thread of its own with a context of its own (sharing the renderer's objects) owns the window: the render thread
   hands it the display buffer and a fence, and goes on with the next picture in the next buffer (three of them).
   Before a buffer is drawn into again, the GPU waits for its showing to have read it. R36_PRESENT=0: as before. */
enum { PB_FREE, PB_QUEUED, PB_SHOWING };
static pthread_mutex_t sPrLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sPrCond = PTHREAD_COND_INITIALIZER;
static struct { int state; GLsync ready, read; } sPb[NDISP + 1]; /* (NDISP: the text screen's texture) */
static struct { int buf; GLuint tex; int w, h; float aspect; GLsync ready; } sPrJob[NDISP + 1];
static int sPrQ[NDISP + 1], sPrQn; /* the order of the queued */
static volatile int sPrBusy;
unsigned gR36Present[3]; /* (for the log) shown, dropped (a newer one came first), render thread waits for a showing */
static void *present_thread(void *arg) {
    static const float kQuad[8] = {-1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};
    GLuint prog;
    GLint uR, uT;
    (void)arg;
    pthread_setname_np(pthread_self(), "r36_present");
    if (!sE.MakeCurrent(sE.dpy, sE.surf, sE.surf, sE.ctxP)) {
        R36_LogLine("r36 gl: the present thread's context not made current\n");
        return NULL;
    }
    if (sE.SwapInterval != NULL) {
        sE.SwapInterval(sE.dpy, gR36Vsync);
    }
    prog = mk_prog(kPresentVert, kPresentFrag);
    uR = p_glGetUniformLocation(prog, "uR");
    uT = p_glGetUniformLocation(prog, "uTex");
    p_glUseProgram(prog);
    p_glUniform1i(uT, 0);
    p_glDisable(GL_DEPTH_TEST);
    p_glDisable(GL_BLEND);
    p_glDisable(GL_SCISSOR_TEST);
    p_glBindBuffer(GL_ARRAY_BUFFER, 0);
    p_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, kQuad);
    p_glEnableVertexAttribArray(0);
    p_glActiveTexture(GL_TEXTURE0);
    for (;;) {
        int b, k;
        float a, sw = 1.0f, sh = 1.0f;
        typeof(sPrJob[0]) j;
        pthread_mutex_lock(&sPrLock);
        while (sPrQn == 0) {
            sPrBusy = 0;
            pthread_cond_broadcast(&sPrCond);
            pthread_cond_wait(&sPrCond, &sPrLock);
        }
        sPrBusy = 1;
        b = sPrQ[0];
        for (k = 1; k < sPrQn; k++) {
            sPrQ[k - 1] = sPrQ[k];
        }
        sPrQn--;
        j = sPrJob[b];
        sPb[b].state = PB_SHOWING;
        pthread_mutex_unlock(&sPrLock);
        if (j.ready != NULL) {
            p_glWaitSync(j.ready, 0, GL_TIMEOUT_IGNORED);
            p_glDeleteSync(j.ready);
        }
        a = j.aspect > 0.0f ? j.aspect : (float)j.w / (float)j.h;
        if ((float)sWinW / (float)sWinH > a) {
            sw = a * (float)sWinH / (float)sWinW;
        } else {
            sh = (float)sWinW / a / (float)sWinH;
        }
        p_glViewport(0, 0, sWinW, sWinH);
        p_glClearColor(0, 0, 0, 1);
        p_glClear(GL_COLOR_BUFFER_BIT);
        p_glUniform4f(uR, sw, sh, 0.0f, 0.0f);
        p_glBindTexture(GL_TEXTURE_2D, j.tex);
        p_glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        p_glBindTexture(GL_TEXTURE_2D, 0);
        {
            GLsync rd = p_glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            p_glFlush();
            pthread_mutex_lock(&sPrLock);
            sPb[b].read = rd; /* (the buffer may be drawn into again once the GPU is past this) */
            pthread_cond_broadcast(&sPrCond);
            pthread_mutex_unlock(&sPrLock);
        }
        sE.SwapBuffers(sE.dpy, sE.surf);
        gR36Present[0]++;
    }
    return NULL;
}
/* (the render context's thread, at start) the window given to the present thread */
static void present_init(void) {
    void *egl = dlopen("libEGL.so", RTLD_NOW | RTLD_NOLOAD);
    void *(*GetDisplay)(void), *(*GetContext)(void), *(*GetSurface)(int);
    void *(*CreateContext)(void *, void *, void *, const int *);
    unsigned (*QueryContext)(void *, void *, int, int *), (*ChooseConfig)(void *, const int *, void **, int, int *);
    const char *(*QueryString)(void *, int);
    const char *e = getenv("R36_PRESENT");
    static const int kCtx[] = {0x3098, 3, 0x3038};
    int id = 0, n = 0;
    void *cfg = NULL;
    pthread_t th;
    if (e != NULL && atoi(e) == 0) {
        return;
    }
    if (egl == NULL) {
        egl = dlopen("libEGL.so.1", RTLD_NOW | RTLD_NOLOAD);
    }
    if (egl == NULL) {
        egl = RTLD_DEFAULT;
    }
#define S(v, nm) *(void **)&v = dlsym(egl, nm)
    S(GetDisplay, "eglGetCurrentDisplay"); S(GetContext, "eglGetCurrentContext"); S(GetSurface, "eglGetCurrentSurface");
    S(CreateContext, "eglCreateContext"); S(QueryContext, "eglQueryContext"); S(ChooseConfig, "eglChooseConfig");
    S(QueryString, "eglQueryString"); S(sE.MakeCurrent, "eglMakeCurrent"); S(sE.SwapBuffers, "eglSwapBuffers");
    S(sE.SwapInterval, "eglSwapInterval");
#undef S
    if (GetDisplay == NULL || GetContext == NULL || GetSurface == NULL || CreateContext == NULL || QueryContext == NULL ||
        ChooseConfig == NULL || sE.MakeCurrent == NULL || sE.SwapBuffers == NULL || p_glWaitSync == NULL || p_glFenceSync == NULL) {
        R36_LogLine("r36 gl: no present thread (EGL calls missing)\n");
        return;
    }
    sE.dpy = GetDisplay();
    if (QueryString == NULL || strstr(QueryString(sE.dpy, 0x3055), "EGL_KHR_surfaceless_context") == NULL) {
        R36_LogLine("r36 gl: no present thread (no surfaceless contexts)\n");
        return;
    }
    sE.ctx = GetContext();
    sE.surf = GetSurface(0x3059 /* EGL_DRAW */);
    QueryContext(sE.dpy, sE.ctx, 0x3028, &id);
    {
        int a[] = {0x3028, id, 0x3038};
        ChooseConfig(sE.dpy, a, &cfg, 1, &n);
    }
    sE.ctxP = n > 0 ? CreateContext(sE.dpy, cfg, sE.ctx, kCtx) : NULL;
    if (sE.ctxP == NULL || sE.surf == NULL) {
        R36_LogLine("r36 gl: no present thread (no second context)\n");
        return;
    }
    p_glFinish();
    if (!sE.MakeCurrent(sE.dpy, NULL, NULL, sE.ctx)) { /* (this context without the window from now on) */
        R36_LogLine("r36 gl: no present thread (the context not current without the window)\n");
        sE.MakeCurrent(sE.dpy, sE.surf, sE.surf, sE.ctx);
        return;
    }
    sPresentOn = 1;
    r_forget();
    pthread_create(&th, NULL, present_thread, NULL);
    pthread_detach(th);
    R36_LogLine("r36 gl: pictures put on the screen by a thread of their own\n");
}
/* (render context) buffer b handed over to be shown; anything older queued and not taken yet is dropped */
extern int gR36NoDrop;
static void present_post(int b, GLuint tex, int w, int h, float aspect) {
    GLsync f = p_glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    int k;
    p_glFlush();
    pthread_mutex_lock(&sPrLock);
    for (k = 0; k < sPrQn && !gR36NoDrop; k++) { /* (a newer picture: the older ones not shown) */
        int o = sPrQ[k];
        if (sPrJob[o].ready != NULL) {
            p_glDeleteSync(sPrJob[o].ready);
            sPrJob[o].ready = NULL;
        }
        sPb[o].state = PB_FREE;
        gR36Present[1]++;
    }
    if (!gR36NoDrop) {
        sPrQn = 0;
    }
    sPrJob[b].buf = b;
    sPrJob[b].tex = tex;
    sPrJob[b].w = w;
    sPrJob[b].h = h;
    sPrJob[b].aspect = aspect;
    sPrJob[b].ready = f;
    sPb[b].state = PB_QUEUED;
    if (sPb[b].read != NULL) { /* (an older showing's fence, waited for before: not needed) */
        p_glDeleteSync(sPb[b].read);
        sPb[b].read = NULL;
    }
    sPrQ[sPrQn++] = b;
    pthread_cond_broadcast(&sPrCond);
    pthread_mutex_unlock(&sPrLock);
}
/* (render context) buffer b about to be drawn into: not while it is shown */
int gR36NoDrop = 0; /* every picture shown, in turn (a queue of two at most): none dropped for a newer one */
static void present_claim(int b) {
    GLsync rd = NULL;
    pthread_mutex_lock(&sPrLock);
    if (gR36NoDrop && sPb[b].state == PB_QUEUED) { /* (not shown yet: waited for, at most 100 ms) */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 100000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        while (sPb[b].state == PB_QUEUED) {
            gR36Present[2]++;
            if (pthread_cond_timedwait(&sPrCond, &sPrLock, &ts) != 0) {
                break;
            }
        }
    }
    if (sPb[b].state == PB_QUEUED) { /* (never taken: dropped) */
        int k, m = 0;
        for (k = 0; k < sPrQn; k++) {
            if (sPrQ[k] != b) {
                sPrQ[m++] = sPrQ[k];
            }
        }
        sPrQn = m;
        if (sPrJob[b].ready != NULL) {
            p_glDeleteSync(sPrJob[b].ready);
            sPrJob[b].ready = NULL;
        }
        gR36Present[1]++;
    } else if (sPb[b].state == PB_SHOWING) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;
        while (sPb[b].read == NULL) { /* (being drawn onto the window right now: a moment) */
            gR36Present[2]++;
            if (pthread_cond_timedwait(&sPrCond, &sPrLock, &ts) != 0) {
                break;
            }
        }
        rd = sPb[b].read;
        sPb[b].read = NULL;
    }
    sPb[b].state = PB_FREE;
    pthread_mutex_unlock(&sPrLock);
    if (rd != NULL) {
        p_glWaitSync(rd, 0, GL_TIMEOUT_IGNORED); /* (the GPU waits, not this thread) */
        p_glDeleteSync(rd);
    }
}
/* (render context) everything handed over shown (before the buffers are made again) */
static void present_drain(void) {
    int b;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 1; /* (a present thread that is gone: not waited for forever) */
    pthread_mutex_lock(&sPrLock);
    while ((sPrQn > 0 || sPrBusy) && pthread_cond_timedwait(&sPrCond, &sPrLock, &ts) == 0) {
    }
    pthread_mutex_unlock(&sPrLock);
    for (b = 0; b <= NDISP; b++) {
        present_claim(b);
    }
    p_glFinish();
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
    r_use(sCurProg);
    attr_restore();
    bind_target();
}
unsigned gR36Frames;
void vglSwapBuffers(GLboolean has_commondialog) {
    ENTER();
    unsigned done = gxm_back_buffer_index % NDISP;
    (void)has_commondialog;
    flush_dirty();
    stream_next();
    sEv |= 128;
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
    {
        unsigned long long t0 = sceKernelGetProcessTimeWide(), t1;
        static unsigned long long sumPresent, sumBetween, last;
        static unsigned n, up0, up1;
        if (discard_on() && !gR36KeepDepth) { /* (the picture's depth and stencil: not stored, the next one clears them) */
            static const GLenum kDS[2] = {GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT};
            r_fbo(sDispFbo[done]);
            p_glInvalidateFramebuffer(GL_FRAMEBUFFER, 2, kDS);
        }
        if (sPresentOn) {
            present_post((int)done, sDispTex[done], sDispW, sDispH, sAspect);
        } else {
            present_tex(sDispTex[done], sDispW, sDispH, 1);
        }
        t1 = sceKernelGetProcessTimeWide();
        sumPresent += t1 - t0;
        if (last != 0) {
            sumBetween += t0 - last;
        }
        last = t1;
        if (++n == 120) {
            R36_LogLine("r36 gl: per picture (120): %u us putting it on the screen (the GPU finishing it), %u us drawing it; %u KB in %u pieces to the GPU from mapped buffers, %u draws\n",
                        (unsigned)(sumPresent / n), (unsigned)(sumBetween / n), (gR36Up[0] - up0) / n / 1024, (gR36Up[1] - up1) / n, (gR36Draws) / n);
            n = 0;
            sumPresent = sumBetween = 0;
            up0 = gR36Up[0];
            up1 = gR36Up[1];
            gR36Draws = 0;
        }
    }
    gR36Frames++;
    {
        extern int gR36TextOn;
        gR36TextOn = 0;
    }
    gxm_front_buffer_index = done;
    gxm_back_buffer_index = (done + 1) % NDISP;
    sScratchAt = 0;
    if (sNewW > 0 && (sNewW != sDispW || sNewH != sDispH)) {
        if (sPresentOn) {
            present_drain();
        }
        disp_alloc(sNewW, sNewH);
    }
    sNewW = sNewH = 0;
    if (sPresentOn) {
        present_claim((int)gxm_back_buffer_index); /* (drawn into next) */
        bind_target();
    } else {
        state_restore();
    }
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
    if (sPresentOn) { /* (by the present thread, twice: both buffers of the window) */
        present_post(NDISP, sTextTex, w, h, (float)w / (float)h);
        present_drain();
        present_post(NDISP, sTextTex, w, h, (float)w / (float)h);
        present_drain();
    } else {
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
#define R36_GL_PASS_V(n, p, a) void r36_##n p { static unsigned e_ = ~0u; ENTER(); if (e_ == ~0u) { e_ = ev_name(#n); } sEv |= e_; p_##n a; LEAVE(); }
R36_GL_PASS_V(glAttachShader, (GLuint program, GLuint shader), (program, shader))
R36_GL_PASS_V(glBindAttribLocation, (GLuint program, GLuint index, const GLchar *name), (program, index, name))
R36_GL_PASS_V(glBlendEquation, (GLenum mode), (mode))
R36_GL_PASS_V(glBlendEquationSeparate, (GLenum modeRGB, GLenum modeAlpha), (modeRGB, modeAlpha))
R36_GL_PASS_V(glBlendFunc, (GLenum sfactor, GLenum dfactor), (sfactor, dfactor))
R36_GL_PASS_V(glBlendFuncSeparate, (GLenum a, GLenum b, GLenum c, GLenum d), (a, b, c, d))
R36_GL_PASS(GLuint, glCreateProgram, (void), ())
R36_GL_PASS_V(glDepthFunc, (GLenum func), (func))
void r36_glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers) {
    ENTER();
    p_glDeleteFramebuffers(n, framebuffers);
    rFbo = R_UNK; /* (the one bound may be gone: 0 in its place) */
    LEAVE();
}
R36_GL_PASS_V(glFinish, (void), ())
R36_GL_PASS_V(glFlush, (void), ())
R36_GL_PASS_V(glGenFramebuffers, (GLsizei n, GLuint *framebuffers), (n, framebuffers))
R36_GL_PASS_V(glGetShaderInfoLog, (GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog), (shader, bufSize, length, infoLog))
R36_GL_PASS_V(glGetShaderiv, (GLuint shader, GLenum pname, GLint *params), (shader, pname, params))
R36_GL_PASS_V(glGetProgramiv, (GLuint program, GLenum pname, GLint *params), (program, pname, params))
R36_GL_PASS_V(glGetProgramInfoLog, (GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog), (program, bufSize, length, infoLog))
R36_GL_PASS_V(glStencilFunc, (GLenum func, GLint ref, GLuint mask), (func, ref, mask))
R36_GL_PASS_V(glStencilOp, (GLenum fail, GLenum zfail, GLenum zpass), (fail, zfail, zpass))
R36_GL_PASS_V(glGenBuffers, (GLsizei n, GLuint *buffers), (n, buffers))
R36_GL_PASS(GLenum, glGetError, (void), ())
R36_GL_PASS(GLenum, glCheckFramebufferStatus, (GLenum target), (target))
