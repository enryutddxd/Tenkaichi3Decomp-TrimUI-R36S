/*
 * R36S / TrimUI: what the GPU's driver costs, measured at start (R36_BENCH=1, bt3.txt bench=1): a draw with nothing
 * changed, and with each kind of change before it (attribute pointers, a texture, a program, uniforms, texture
 * parameters, the blend, the framebuffer), one large draw for the GPU's vertex rate, and the same draws made by two
 * threads at once with a context each (whether the driver lets two threads work at the same time). Into the log, as
 * microseconds a draw: the time the calls took on the processor, and the GPU's time to finish them after.
 */
#include <SDL2/SDL.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define GL_GLES_PROTOTYPES 0
#include <GLES3/gl32.h>

extern void R36_LogLine(const char *fmt, ...);

#define BL(X) \
    X(void, glGenBuffers, (GLsizei, GLuint *)) X(void, glBindBuffer, (GLenum, GLuint)) X(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum)) \
    X(void, glGenTextures, (GLsizei, GLuint *)) X(void, glBindTexture, (GLenum, GLuint)) \
    X(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
    X(void, glTexParameteri, (GLenum, GLenum, GLint)) X(void, glGenFramebuffers, (GLsizei, GLuint *)) \
    X(void, glBindFramebuffer, (GLenum, GLuint)) X(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(void, glGenRenderbuffers, (GLsizei, GLuint *)) X(void, glBindRenderbuffer, (GLenum, GLuint)) \
    X(void, glRenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei)) X(void, glFramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint)) \
    X(GLuint, glCreateShader, (GLenum)) X(void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
    X(void, glCompileShader, (GLuint)) X(GLuint, glCreateProgram, (void)) X(void, glAttachShader, (GLuint, GLuint)) \
    X(void, glLinkProgram, (GLuint)) X(void, glUseProgram, (GLuint)) X(GLint, glGetUniformLocation, (GLuint, const GLchar *)) \
    X(void, glUniform4fv, (GLint, GLsizei, const GLfloat *)) X(void, glUniform1i, (GLint, GLint)) \
    X(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) X(void, glEnableVertexAttribArray, (GLuint)) \
    X(void, glDisableVertexAttribArray, (GLuint)) X(void, glDrawArrays, (GLenum, GLint, GLsizei)) \
    X(void, glDrawElements, (GLenum, GLsizei, GLenum, const void *)) X(void, glDrawElementsBaseVertex, (GLenum, GLsizei, GLenum, const void *, GLint)) \
    X(void, glFinish, (void)) X(void, glFlush, (void)) X(void, glViewport, (GLint, GLint, GLsizei, GLsizei)) X(void, glEnable, (GLenum)) \
    X(void, glDisable, (GLenum)) X(void, glBlendFunc, (GLenum, GLenum)) X(void, glClear, (GLbitfield)) X(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, glDeleteBuffers, (GLsizei, const GLuint *)) X(void, glDeleteTextures, (GLsizei, const GLuint *)) X(void, glDeleteFramebuffers, (GLsizei, const GLuint *)) \
    X(void, glDeleteRenderbuffers, (GLsizei, const GLuint *)) X(void, glDeleteProgram, (GLuint)) X(void, glGetProgramiv, (GLuint, GLenum, GLint *)) \
    X(void, glGetShaderiv, (GLuint, GLenum, GLint *)) X(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(const GLubyte *, glGetString, (GLenum)) X(void, glDepthFunc, (GLenum)) X(void, glUniformBlockBinding, (GLuint, GLuint, GLuint)) \
    X(GLuint, glGetUniformBlockIndex, (GLuint, const GLchar *)) X(void, glBindBufferRange, (GLenum, GLuint, GLuint, GLintptr, GLsizeiptr)) \
    X(void, glBufferSubData, (GLenum, GLintptr, GLsizeiptr, const void *)) X(void, glActiveTexture, (GLenum))
#define X(r, n, p) static r(*b_##n) p;
BL(X)
#undef X

static double now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e6 + (double)t.tv_nsec / 1e3;
}

#define NU 368
static const char *kVs =
    "#version 300 es\n"
    "layout(location=0) in vec4 aPos; layout(location=1) in vec4 aCol; layout(location=2) in vec2 aTex;\n"
    "uniform vec4 U[NUV];\n"
    "out vec4 vCol; out vec2 vTex;\n"
    "void main() { int i = int(aCol.a * 255.0) % NUV; gl_Position = aPos + U[i] * 0.0001; vCol = aCol; vTex = aTex; }\n";
static const char *kVsUbo =
    "#version 300 es\n"
    "layout(location=0) in vec4 aPos; layout(location=1) in vec4 aCol; layout(location=2) in vec2 aTex;\n"
    "layout(std140) uniform B { vec4 U[368]; };\n"
    "out vec4 vCol; out vec2 vTex;\n"
    "void main() { int i = int(aCol.a * 255.0) % 368; gl_Position = aPos + U[i] * 0.0001; vCol = aCol; vTex = aTex; }\n";
static const char *kFs =
    "#version 300 es\nprecision mediump float;\n"
    "uniform sampler2D uTex; in vec4 vCol; in vec2 vTex; out vec4 o;\n"
    "void main() { o = texture(uTex, vTex) * vCol; }\n";

static GLuint mk(const char *vs, int nu) {
    char buf[1024], log[400];
    const char *src;
    GLuint v = b_glCreateShader(GL_VERTEX_SHADER), f = b_glCreateShader(GL_FRAGMENT_SHADER), p = b_glCreateProgram();
    GLint ok = 0;
    char *q;
    snprintf(buf, sizeof(buf), "%s", vs);
    while (nu > 0 && (q = strstr(buf, "NUV")) != NULL) { /* (the array's size into the source) */
        char tmp[1024];
        snprintf(tmp, sizeof(tmp), "%.*s%d%s", (int)(q - buf), buf, nu, q + 3);
        snprintf(buf, sizeof(buf), "%s", tmp);
    }
    src = buf;
    b_glShaderSource(v, 1, &src, NULL);
    b_glCompileShader(v);
    b_glGetShaderiv(v, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        b_glGetShaderInfoLog(v, sizeof(log), NULL, log);
        R36_LogLine("bench: vertex shader: %s\n", log);
    }
    b_glShaderSource(f, 1, &kFs, NULL);
    b_glCompileShader(f);
    b_glAttachShader(p, v);
    b_glAttachShader(p, f);
    b_glLinkProgram(p);
    b_glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        R36_LogLine("bench: a program not linked\n");
    }
    return p;
}

typedef struct { float x, y, z, w, s, t, q; uint8_t c[4]; } BV;
#define NV (3 * 6000)
static GLuint sVbo, sIbo, sTex[2], sProgA, sProgB, sProgU, sUbo, sFbo[2], sCol[2], sDep[2];
static GLint sUA, sUB;
static float sU[NU * 4];
static uint16_t sIdx[NV];

static void attrs(GLintptr off) {
    b_glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 32, (const void *)(off + 0));
    b_glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, (const void *)(off + 28));
    b_glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (const void *)(off + 16));
}
static void fbo_make(int k, int w, int h) {
    b_glGenTextures(1, &sCol[k]);
    b_glBindTexture(GL_TEXTURE_2D, sCol[k]);
    b_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    b_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    b_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    b_glGenRenderbuffers(1, &sDep[k]);
    b_glBindRenderbuffer(GL_RENDERBUFFER, sDep[k]);
    b_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    b_glGenFramebuffers(1, &sFbo[k]);
    b_glBindFramebuffer(GL_FRAMEBUFFER, sFbo[k]);
    b_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sCol[k], 0);
    b_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, sDep[k]);
}
/* the usual state for a test: the large picture bound and cleared, program A, texture 0, the attributes at 0 */
static void base_state(GLuint fbo) {
    b_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    b_glViewport(0, 0, 960, 544);
    b_glClearColor(0, 0, 0, 1);
    b_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    b_glUseProgram(sProgA);
    b_glActiveTexture(GL_TEXTURE0);
    b_glBindTexture(GL_TEXTURE_2D, sTex[0]);
    b_glBindBuffer(GL_ARRAY_BUFFER, sVbo);
    b_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sIbo);
    attrs(0);
    b_glEnableVertexAttribArray(0);
    b_glEnableVertexAttribArray(1);
    b_glEnableVertexAttribArray(2);
    b_glEnable(GL_DEPTH_TEST);
    b_glDepthFunc(GL_ALWAYS);
    b_glDisable(GL_BLEND);
}

enum { T_SAME, T_VAP, T_FIRST, T_BASEV, T_CLIENTIDX, T_TEX, T_PROG, T_U16, T_U368, T_UBO, T_TP, T_BLEND, T_FBO, T_BIG, T_N };
static const char *kName[T_N] = {"nothing changed", "attribute pointers", "first vertex", "base vertex (indices)", "indices from memory",
                                 "texture bound", "program", "16 uniform vectors", "368 uniform vectors", "uniform buffer range",
                                 "texture parameters", "blend on/off", "framebuffer every 20 draws", "large draws (3000 triangles)"};
#define ND 2000
static void run_test(int t, GLuint fbo, double *cpu, double *gpu) {
    int i, n = t == T_BIG ? 100 : ND;
    double t0, t1, t2;
    base_state(fbo);
    if (t == T_UBO) {
        b_glUseProgram(sProgU);
    }
    b_glFinish();
    t0 = now_us();
    for (i = 0; i < n; i++) {
        int f = (i % 6000) * 3; /* (a different triangle each draw) */
        switch (t) {
        case T_SAME: b_glDrawArrays(GL_TRIANGLES, 0, 3); break;
        case T_VAP: attrs((GLintptr)f * 32); b_glDrawArrays(GL_TRIANGLES, 0, 3); break;
        case T_FIRST: b_glDrawArrays(GL_TRIANGLES, f, 3); break;
        case T_BASEV:
            if (b_glDrawElementsBaseVertex != NULL) {
                b_glDrawElementsBaseVertex(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, (const void *)0, f);
            }
            break;
        case T_CLIENTIDX:
            b_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            b_glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, sIdx + f);
            break;
        case T_TEX: b_glBindTexture(GL_TEXTURE_2D, sTex[i & 1]); b_glDrawArrays(GL_TRIANGLES, f, 3); break;
        case T_PROG: b_glUseProgram((i & 1) ? sProgB : sProgA); b_glDrawArrays(GL_TRIANGLES, f, 3); break;
        case T_U16: sU[0] = (float)i; b_glUniform4fv(sUA, 16, sU); b_glDrawArrays(GL_TRIANGLES, f, 3); break;
        case T_U368: sU[0] = (float)i; b_glUniform4fv(sUA, NU, sU); b_glDrawArrays(GL_TRIANGLES, f, 3); break;
        case T_UBO: b_glBindBufferRange(GL_UNIFORM_BUFFER, 0, sUbo, (GLintptr)(i % 64) * 6144, 368 * 16); b_glDrawArrays(GL_TRIANGLES, f, 3); break;
        case T_TP: b_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (i & 1) ? GL_LINEAR : GL_NEAREST); b_glDrawArrays(GL_TRIANGLES, f, 3); break;
        case T_BLEND: if (i & 1) { b_glEnable(GL_BLEND); } else { b_glDisable(GL_BLEND); } b_glDrawArrays(GL_TRIANGLES, f, 3); break;
        case T_FBO:
            if (i % 20 == 0) {
                b_glBindFramebuffer(GL_FRAMEBUFFER, (i / 20) & 1 ? sFbo[1] : fbo);
            }
            b_glDrawArrays(GL_TRIANGLES, f, 3);
            break;
        case T_BIG: b_glDrawArrays(GL_TRIANGLES, 0, 9000); break;
        }
    }
    t1 = now_us();
    b_glFinish();
    t2 = now_us();
    *cpu = (t1 - t0) / n;
    *gpu = (t2 - t1) / 1000.0;
}

/* ---- two threads at once: a second context (sharing the objects) on a second thread ---- */
static struct {
    void *dpy, *ctx, *surf;
    unsigned (*MakeCurrent)(void *, void *, void *, void *);
    pthread_barrier_t go;
    double cpu, gpu;
    int ok;
} sW;
static void *worker(void *arg) {
    GLuint fbo = 0, col = 0, dep = 0;
    (void)arg;
    sW.ok = sW.MakeCurrent(sW.dpy, sW.surf, sW.surf, sW.ctx) != 0;
    if (sW.ok) { /* (a framebuffer of its own: those are not shared) */
        b_glGenTextures(1, &col);
        b_glBindTexture(GL_TEXTURE_2D, col);
        b_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 960, 544, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        b_glGenRenderbuffers(1, &dep);
        b_glBindRenderbuffer(GL_RENDERBUFFER, dep);
        b_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, 960, 544);
        b_glGenFramebuffers(1, &fbo);
        b_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        b_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, col, 0);
        b_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, dep);
        b_glFinish();
    }
    pthread_barrier_wait(&sW.go);
    if (sW.ok) {
        run_test(T_TEX, fbo, &sW.cpu, &sW.gpu);
    }
    pthread_barrier_wait(&sW.go);
    if (sW.ok) {
        b_glDeleteFramebuffers(1, &fbo);
        b_glDeleteRenderbuffers(1, &dep);
        b_glDeleteTextures(1, &col);
        b_glFinish();
        sW.MakeCurrent(sW.dpy, NULL, NULL, NULL);
    }
    return NULL;
}
static void two_threads(void) {
    void *egl = dlopen("libEGL.so", RTLD_NOW | RTLD_NOLOAD);
    void *(*GetDisplay)(void), *(*GetContext)(void);
    void *(*CreateContext)(void *, void *, void *, const int *), *(*CreatePbuffer)(void *, void *, const int *);
    unsigned (*QueryContext)(void *, void *, int, int *), (*ChooseConfig)(void *, const int *, void **, int, int *);
    unsigned (*DestroyContext)(void *, void *), (*DestroySurface)(void *, void *);
    int (*GetErr)(void);
    const char *(*QueryString)(void *, int);
    int id = 0, n = 0;
    void *cfg = NULL;
    static const int kCtx[] = {0x3098 /* CLIENT_VERSION */, 3, 0x3038};
    static const int kPb[] = {0x3057, 16, 0x3056, 16, 0x3038};
    pthread_t th;
    double c1, g1;
    if (egl == NULL) {
        egl = dlopen("libEGL.so.1", RTLD_NOW | RTLD_NOLOAD);
    }
    if (egl == NULL) {
        egl = RTLD_DEFAULT;
    }
#define S(v, nm) *(void **)&v = dlsym(egl, nm)
    S(GetDisplay, "eglGetCurrentDisplay"); S(GetContext, "eglGetCurrentContext"); S(CreateContext, "eglCreateContext");
    S(CreatePbuffer, "eglCreatePbufferSurface"); S(QueryContext, "eglQueryContext"); S(ChooseConfig, "eglChooseConfig");
    S(DestroyContext, "eglDestroyContext"); S(DestroySurface, "eglDestroySurface"); S(GetErr, "eglGetError");
    S(QueryString, "eglQueryString"); S(sW.MakeCurrent, "eglMakeCurrent");
#undef S
    if (GetDisplay == NULL || CreateContext == NULL || sW.MakeCurrent == NULL || QueryContext == NULL || ChooseConfig == NULL) {
        R36_LogLine("bench: two threads: no EGL calls found\n");
        return;
    }
    sW.dpy = GetDisplay();
    if (QueryString != NULL) {
        R36_LogLine("bench: EGL extensions: %s\n", QueryString(sW.dpy, 0x3055));
    }
    QueryContext(sW.dpy, GetContext(), 0x3028 /* CONFIG_ID */, &id);
    {
        int a[] = {0x3028, id, 0x3038};
        ChooseConfig(sW.dpy, a, &cfg, 1, &n);
    }
    sW.ctx = n > 0 ? CreateContext(sW.dpy, cfg, GetContext(), kCtx) : NULL;
    if (sW.ctx == NULL) {
        R36_LogLine("bench: two threads: no second context (EGL error %x)\n", GetErr != NULL ? GetErr() : 0);
        return;
    }
    sW.surf = NULL; /* (none: EGL_KHR_surfaceless_context), else a small pbuffer */
    {
        void *keepS = NULL;
        pthread_barrier_init(&sW.go, NULL, 2);
        /* the surfaceless way tried by the worker; when it fails, a pbuffer */
        if (QueryString != NULL && strstr(QueryString(sW.dpy, 0x3055), "surfaceless") == NULL && CreatePbuffer != NULL) {
            sW.surf = keepS = CreatePbuffer(sW.dpy, cfg, kPb);
        }
        pthread_create(&th, NULL, worker, NULL);
        pthread_barrier_wait(&sW.go);
        run_test(T_TEX, sFbo[0], &c1, &g1);
        pthread_barrier_wait(&sW.go);
        pthread_join(th, NULL);
        pthread_barrier_destroy(&sW.go);
        if (sW.ok) {
            R36_LogLine("bench: two threads at once, texture changes: this thread %.1f us a draw (GPU %.1f ms after), the other %.1f us (GPU %.1f ms)\n",
                        c1, g1, sW.cpu, sW.gpu);
        } else {
            R36_LogLine("bench: two threads: the second context not made current (EGL error %x)\n", GetErr != NULL ? GetErr() : 0);
        }
        DestroyContext(sW.dpy, sW.ctx);
        if (keepS != NULL && DestroySurface != NULL) {
            DestroySurface(sW.dpy, keepS);
        }
    }
}

void R36_Bench(void) {
    int t, k, i;
    BV *v;
    uint32_t px[64 * 64];
    double cpu, gpu, best[T_N][2];
#define X(r, n, p) b_##n = (r(*) p)SDL_GL_GetProcAddress(#n);
    BL(X)
#undef X
    R36_LogLine("bench: GL extensions: %s\n", (const char *)b_glGetString(GL_EXTENSIONS));
    v = (BV *)calloc(NV, sizeof(BV));
    for (i = 0; i < NV; i++) { /* small triangles over the picture */
        int tri = i / 3, c = i % 3;
        float x = -0.95f + 1.9f * (float)(tri % 97) / 97.0f, y = -0.95f + 1.9f * (float)((tri / 97) % 61) / 61.0f;
        v[i].x = x + (c == 1 ? 0.01f : 0.0f);
        v[i].y = y + (c == 2 ? 0.015f : 0.0f);
        v[i].z = 0.5f;
        v[i].w = 1.0f;
        v[i].s = c == 1 ? 1.0f : 0.0f;
        v[i].t = c == 2 ? 1.0f : 0.0f;
        v[i].c[0] = v[i].c[1] = v[i].c[2] = 200;
        v[i].c[3] = (uint8_t)(tri & 255);
        sIdx[i] = (uint16_t)(i % 3);
    }
    for (i = 0; i < 64 * 64; i++) {
        px[i] = 0xFF000000u | (uint32_t)i * 2654435761u;
    }
    for (i = 0; i < NU * 4; i++) {
        sU[i] = (float)(i % 7);
    }
    b_glGenBuffers(1, &sVbo);
    b_glBindBuffer(GL_ARRAY_BUFFER, sVbo);
    b_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(NV * sizeof(BV)), v, GL_STATIC_DRAW);
    b_glGenBuffers(1, &sIbo);
    b_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sIbo);
    b_glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)sizeof(sIdx), sIdx, GL_STATIC_DRAW);
    b_glGenBuffers(1, &sUbo);
    b_glBindBuffer(GL_UNIFORM_BUFFER, sUbo);
    {
        float *u = (float *)calloc(64 * 6144 / 4, 4);
        b_glBufferData(GL_UNIFORM_BUFFER, 64 * 6144, u, GL_STATIC_DRAW);
        free(u);
    }
    b_glBindBuffer(GL_UNIFORM_BUFFER, 0);
    free(v);
    for (k = 0; k < 2; k++) {
        b_glGenTextures(1, &sTex[k]);
        b_glBindTexture(GL_TEXTURE_2D, sTex[k]);
        b_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        b_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        b_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    sProgA = mk(kVs, NU);
    sProgB = mk(kVs, 8);
    sProgU = mk(kVsUbo, 0);
    sUA = b_glGetUniformLocation(sProgA, "U");
    sUB = b_glGetUniformLocation(sProgB, "U");
    b_glUseProgram(sProgA);
    b_glUniform1i(b_glGetUniformLocation(sProgA, "uTex"), 0);
    b_glUniform4fv(sUA, NU, sU);
    b_glUseProgram(sProgB);
    b_glUniform1i(b_glGetUniformLocation(sProgB, "uTex"), 0);
    b_glUniform4fv(sUB, 8, sU);
    b_glUseProgram(sProgU);
    b_glUniform1i(b_glGetUniformLocation(sProgU, "uTex"), 0);
    b_glUniformBlockBinding(sProgU, b_glGetUniformBlockIndex(sProgU, "B"), 0);
    fbo_make(0, 960, 544);
    fbo_make(1, 256, 256);
    for (t = 0; t < T_N; t++) {
        best[t][0] = best[t][1] = 1e18;
        for (k = 0; k < 3; k++) { /* (the best of three) */
            run_test(t, sFbo[0], &cpu, &gpu);
            if (cpu < best[t][0]) {
                best[t][0] = cpu;
                best[t][1] = gpu;
            }
        }
        R36_LogLine("bench: %-30s %7.1f us a draw on the processor, then %6.1f ms for the GPU to finish%s\n", kName[t], best[t][0], best[t][1],
                    t == T_BIG ? " (100 draws)" : " (2000 draws)");
    }
    two_threads();
    /* (everything let go; the state as a new context has it, for the renderer) */
    b_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    b_glUseProgram(0);
    b_glBindBuffer(GL_ARRAY_BUFFER, 0);
    b_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    b_glBindBuffer(GL_UNIFORM_BUFFER, 0);
    b_glBindTexture(GL_TEXTURE_2D, 0);
    b_glDisableVertexAttribArray(0);
    b_glDisableVertexAttribArray(1);
    b_glDisableVertexAttribArray(2);
    b_glDisable(GL_DEPTH_TEST);
    b_glDepthFunc(GL_LESS);
    b_glViewport(0, 0, 960, 544);
    b_glDeleteFramebuffers(2, sFbo);
    b_glDeleteRenderbuffers(2, sDep);
    b_glDeleteTextures(2, sCol);
    b_glDeleteTextures(2, sTex);
    b_glDeleteBuffers(1, &sVbo);
    b_glDeleteBuffers(1, &sIbo);
    b_glDeleteBuffers(1, &sUbo);
    b_glDeleteProgram(sProgA);
    b_glDeleteProgram(sProgB);
    b_glDeleteProgram(sProgU);
    b_glFinish();
}
