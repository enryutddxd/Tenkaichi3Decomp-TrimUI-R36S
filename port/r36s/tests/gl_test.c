/*
 * A test of the R36S's vitaGL layer (port/r36s/shim) on the build machine: OpenGL ES 3 by Mesa in a virtual X
 * display (Xvfb), the same calls as port/vita/vita_vgl.c makes, and the pixels read back.
 *
 *   port/r36s/tests/gl_test.sh
 *
 * Checked: the Cg shaders of vita_vgl.c translated and linked; the display drawn upside down (as vitaGL's memory)
 * and shown the right way up; the viewport and scissor on the display; the display read as a texture; a
 * framebuffer object; a mapped buffer written and drawn from; vglFastDraw's uniform block; the fixed-function path
 * (the films' pictures); the text screen printed from another thread while the drawing thread holds the context.
 */
#include <SDL2/SDL.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "vitaGL.h"
#include "debugScreen.h"

void Vita_Log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}
extern void R36_GlYield(void);
void Vita_CrashLog(int fd) { (void)fd; }

static int sFail;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: " __VA_ARGS__); printf("\n"); sFail++; } else { printf("ok: " __VA_ARGS__); printf("\n"); } } while (0)

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    long n;
    char *b;
    if (f == NULL) {
        printf("missing %s (run port/r36s/tests/shader_check.py first)\n", path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = malloc((size_t)n + 1);
    fread(b, 1, (size_t)n, f);
    b[n] = 0;
    fclose(f);
    return b;
}
static GLuint prog(const char *vsName, const char *fsName, const char *a0, const char *a1, const char *a2) {
    char path[300];
    char *vs, *fs;
    GLuint v = glCreateShader(GL_CG_VERTEX_SHADER_EXT), f = glCreateShader(GL_CG_FRAGMENT_SHADER_EXT), p;
    GLint ok = 0;
    snprintf(path, sizeof(path), "port/build/shader_check/%s.cg", vsName);
    vs = slurp(path);
    snprintf(path, sizeof(path), "port/build/shader_check/%s.cg", fsName);
    fs = slurp(path);
    glShaderSource(v, 1, (const char *const *)&vs, NULL);
    glCompileShader(v);
    glGetShaderiv(v, GL_COMPILE_STATUS, &ok);
    CHECK(ok, "vertex shader %s compiled", vsName);
    glShaderSource(f, 1, (const char *const *)&fs, NULL);
    glCompileShader(f);
    glGetShaderiv(f, GL_COMPILE_STATUS, &ok);
    CHECK(ok, "pixel shader %s compiled", fsName);
    p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glBindAttribLocation(p, 0, a0);
    if (a1) { glBindAttribLocation(p, 1, a1); }
    if (a2) { glBindAttribLocation(p, 2, a2); }
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    CHECK(ok, "program %s + %s linked", vsName, fsName);
    return p;
}

/* the real glReadPixels (the shim has none: the renderer reads nothing back) */
static void (*readPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
static void (*realBindFb)(GLenum, GLuint);
static void (*realGetIv)(GLenum, GLint *);
static uint32_t px_at(int x, int y) {
    uint32_t c = 0;
    readPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &c);
    return c;
}
#define RED 0xFF0000FFu
#define GREEN 0xFF00FF00u
#define BLUE 0xFFFF0000u
#define BLACK 0xFF000000u

typedef struct V { float x, y, z, w, s, t, q; uint8_t c[4]; } V;
static void quad(V *v, float x0, float y0, float x1, float y1, uint32_t rgba) { /* two triangles, colour with 0x80 = 1 */
    float xs[6] = {x0, x1, x0, x1, x1, x0}, ys[6] = {y0, y0, y1, y0, y1, y1};
    for (int i = 0; i < 6; i++) {
        v[i].x = xs[i]; v[i].y = ys[i]; v[i].z = 0.0f; v[i].w = 1.0f;
        v[i].s = xs[i] * 0.5f + 0.5f; v[i].t = 0.5f - ys[i] * 0.5f; v[i].q = 1.0f;
        memcpy(v[i].c, &rgba, 4);
    }
}
static int sCbCalls;
static void display_cb(void *fb) { (void)fb; sCbCalls++; }
static volatile int sDrawerReady, sTextDone;

/* the render thread of 8b */
extern void (*gR36CtxWake)(void);
extern void R36_GlKeep(void), R36_GlRelease(void);
extern unsigned long long R36_NowNs(void);
static SceUID sRtSema;
static volatile int sRtGive, sRtHas, sRtQuit;
static void rt_wake(void) { sRtGive = 1; sceKernelSignalSema(sRtSema, 1); }
static void *rt_main(void *a) {
    (void)a;
    R36_GlKeep();
    glClear(GL_COLOR_BUFFER_BIT); /* (taken: kept) */
    sRtHas = 1;
    while (!sRtQuit) {
        sceKernelWaitSema(sRtSema, 1, NULL); /* (asleep with the context) */
        if (sRtGive) {
            sRtGive = 0;
            R36_GlRelease();
        }
    }
    return NULL;
}

static void *drawer(void *arg) {
    V v[6];
    GLuint p, pc, ff;
    (void)arg;
    CHECK(vglInitExtended(0, 640, 480, 0, SCE_GXM_MULTISAMPLE_NONE), "vglInitExtended");
    readPixels = SDL_GL_GetProcAddress("glReadPixels");
    realBindFb = SDL_GL_GetProcAddress("glBindFramebuffer");
    realGetIv = SDL_GL_GetProcAddress("glGetIntegerv");
    vglSetDisplayCallback(display_cb);

    /* 1. the GS's shader pair: a red quad over the top half of the display */
    p = prog("kVert", "kFrag", "aPos", "aCol", "aTex");
    glUseProgram(p);
    glUniform4f(glGetUniformLocation(p, "uMode"), 0, 0, 0, 0);
    glUniform4f(glGetUniformLocation(p, "uTest"), 0, 0, 0, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, 640, 480);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    quad(v, -1, 0, 1, 1, 0x80000080u); /* red, alpha 0x80 */
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(V), &v[0].x);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(V), &v[0].c[0]);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(V), &v[0].s);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    /* the display's memory: row 0 is the top (as vitaGL's) */
    CHECK(px_at(5, 2) == RED, "display row 0 (top of the screen) red: %08x", px_at(5, 2));
    CHECK(px_at(5, 477) == BLACK, "display last row black: %08x", px_at(5, 477));

    /* 2. a viewport in GL's bottom-left quarter: on the screen's bottom left, in memory the last rows */
    glViewport(0, 0, 320, 240);
    quad(v, -1, -1, 1, 1, 0x80008000u);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(V), &v[0].x);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(V), &v[0].c[0]);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    CHECK(px_at(5, 475) == GREEN, "bottom-left viewport: memory's last rows green: %08x", px_at(5, 475));
    CHECK(px_at(5, 2) == RED, "top still red: %08x", px_at(5, 2));
    CHECK(px_at(600, 475) == BLACK, "bottom right still black: %08x", px_at(600, 475));

    /* 3. a scissor at GL's top right (x 320.., y 240..): the screen's top right, memory rows 0..239 */
    glViewport(0, 0, 640, 480);
    glEnable(GL_SCISSOR_TEST);
    glScissor(320, 240, 320, 240);
    quad(v, -1, -1, 1, 1, 0x80800000u); /* blue */
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(V), &v[0].x);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(V), &v[0].c[0]);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisable(GL_SCISSOR_TEST);
    CHECK(px_at(630, 5) == BLUE, "top-right scissor: memory's first rows blue on the right: %08x", px_at(630, 5));
    CHECK(px_at(5, 5) == RED, "top left still red: %08x", px_at(5, 5));

    /* 4. the display as a texture, copied into a framebuffer object of ours turned over (ids_exec's copy) */
    {
        GLuint view, tex, fbo;
        static const float kQuad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
        pc = prog("kVertFx", "kFragCopy", "aPos", NULL, NULL);
        glGenTextures(1, &view);
        glBindTexture(GL_TEXTURE_2D, view);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        vglSetupTexture(GL_TEXTURE_2D, gxm_color_surfaces_addr[gxm_back_buffer_index], SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, SCE_GXM_TEXTURE_LINEAR, 640, 480, 1);
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 640, 480, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "framebuffer object complete");
        glViewport(0, 0, 640, 480);
        glUseProgram(pc);
        glBindTexture(GL_TEXTURE_2D, view);
        glUniform1i(glGetUniformLocation(pc, "uSrc"), 0);
        glUniform4f(glGetUniformLocation(pc, "uV"), 0.0f, 1.0f, -1.0f, 0.0f);
        glDisableVertexAttribArray(1);
        glDisableVertexAttribArray(2);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, kQuad);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        /* "its rows are the display's": row 0 of the copy is the display's row 0 (red), the last its last (green) */
        CHECK(px_at(5, 2) == RED, "copy of the display: row 0 red: %08x", px_at(5, 2));
        CHECK(px_at(5, 477) == GREEN, "copy of the display: last row green: %08x", px_at(5, 477));
        CHECK(px_at(630, 5) == BLUE, "copy of the display: first rows blue on the right: %08x", px_at(630, 5));
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    /* 5. a mapped buffer: written after the map (vglR36Dirty), drawn from */
    {
        GLuint vbo;
        V *m;
        glUseProgram(p);
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, 4096, NULL, GL_STATIC_DRAW);
        m = (V *)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
        glUnmapBuffer(GL_ARRAY_BUFFER);
        CHECK(m != NULL, "glMapBuffer gave memory");
        quad(m + 10, -1, -1, -0.5f, -0.5f, 0x80800000u); /* blue, the bottom-left corner of the screen */
        vglR36Dirty(m + 10, 6 * sizeof(V));
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(V), (void *)(10 * sizeof(V)));
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(V), (void *)(10 * sizeof(V) + 28));
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(V), (void *)(10 * sizeof(V) + 16));
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        CHECK(px_at(5, 475) == BLUE, "mapped buffer drawn: bottom left blue: %08x", px_at(5, 475));
    }

    /* 6. the fixed-function path (a film's picture): a texture of one green pixel over the screen */
    {
        GLuint t;
        uint32_t g = GREEN;
        static const float kPos[8] = {-1, 1, 1, 1, -1, -1, 1, -1}, kUv[8] = {0, 0, 1, 0, 0, 1, 1, 1};
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &g);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glUseProgram(0);
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glEnableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glColor4f(1, 1, 1, 1);
        glVertexPointer(2, GL_FLOAT, 0, kPos);
        glTexCoordPointer(2, GL_FLOAT, 0, kUv);
        glViewport(0, 0, 640, 480);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        CHECK(px_at(320, 240) == GREEN, "fixed-function draw: green: %08x", px_at(320, 240));
        glDisable(GL_TEXTURE_2D);
    }

    /* 7. vglFastDraw: the direct path's stage shader with its block U[6], indices in an element buffer */
    {
        GLuint pd = prog("kVertStD", "variant_0000", "aPos", "aCol", "aTex"), ibo;
        static const uint16_t kIx[6] = {0, 1, 2, 1, 3, 2};
        static float verts[4][8]; /* x y z w, s t, colour (4 bytes), pad */
        float U[6][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}, {1, 1, 1, 1}, {0, 0, 0, 0}};
        uint32_t red = 0x80000080u;
        const float xy[4][2] = {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
        uint32_t usize = vglFastVertUniformSize(pd);
        CHECK(usize == 6 * 16, "vglFastVertUniformSize = %u", usize);
        CHECK(vglFastUniformOffset(pd, glGetUniformLocation(pd, "U")) == 0, "vglFastUniformOffset 0");
        for (int i = 0; i < 4; i++) {
            verts[i][0] = xy[i][0]; verts[i][1] = xy[i][1]; verts[i][2] = 0; verts[i][3] = 1;
            verts[i][4] = 0; verts[i][5] = 0;
            memcpy(&verts[i][6], &red, 4);
        }
        glGenBuffers(1, &ibo);
        glUseProgram(pd);
        glUniform4f(glGetUniformLocation(pd, "uTest"), 0, 0, 0, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kIx), kIx, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 32, &verts[0][0]);
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, &verts[0][6]);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, &verts[0][4]);
        CHECK(vglFastPrepare(GL_UNSIGNED_SHORT), "vglFastPrepare");
        vglFastDraw(GL_UNSIGNED_SHORT, 0, 6, U, usize);
        CHECK(px_at(320, 240) == RED, "vglFastDraw: red over the screen: %08x", px_at(320, 240));
        /* indices in main memory (vglFastDrawPtr), then the element buffer's again: the left half black, then red over all */
        {
            static const uint16_t kLeft[3] = {0, 2, 1};
            static uint32_t blue[4][8];
            for (int i = 0; i < 4; i++) { blue[i][0] = 0x80800000u; }
            glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, &blue[0][0]);
            vglFastDrawPtr(GL_UNSIGNED_SHORT, kLeft, 3, U, usize);
            CHECK(px_at(5, 475) == BLUE, "vglFastDrawPtr: blue at the bottom left: %08x", px_at(5, 475));
            CHECK(px_at(635, 5) == RED, "vglFastDrawPtr: top right still red: %08x", px_at(635, 5));
            glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, &verts[0][6]);
            vglFastDraw(GL_UNSIGNED_SHORT, 0, 6, U, usize);
            CHECK(px_at(5, 475) == RED, "vglFastDraw after it: red again (its element buffer): %08x", px_at(5, 475));
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    /* 7c. the R36S's batched direct path (vita_vgl.c dp_draw_batched): two parts in one draw, each with its own
       constants in its own slot, found by the vertex's number; a slot's scale 0 puts its part away */
    {
        GLuint pb = prog("kVertStB", "variant_0000", "aPos", "aCol", "aTex");
        static float vb[8][8];
        static const uint16_t kIx[12] = {0, 1, 2, 1, 3, 2, 4, 5, 6, 5, 7, 6};
        const float xy[8][2] = {{-1, -1}, {0, -1}, {-1, 1}, {0, 1}, {0, -1}, {1, -1}, {0, 1}, {1, 1}};
        uint32_t red = 0x80000080u;
        float U[2][7][4];
        GLint loc = glGetUniformLocation(pb, "U");
        for (int i = 0; i < 8; i++) {
            vb[i][0] = xy[i][0]; vb[i][1] = xy[i][1]; vb[i][2] = 0; vb[i][3] = 1; vb[i][4] = 0; vb[i][5] = 0;
            memcpy(&vb[i][6], &red, 4);
        }
        memset(U, 0, sizeof(U));
        for (int k = 0; k < 2; k++) {
            for (int j = 0; j < 4; j++) { U[k][j][j] = 1; }
            U[k][6][0] = (float)(k * 4); U[k][6][1] = (float)(k * 4 + 3);
        }
        U[0][6][2] = 2; /* (two slots) */
        U[0][4][0] = U[0][4][1] = U[0][4][2] = U[0][4][3] = 1; /* slot 0 drawn, slot 1 scale 0: put away */
        glUseProgram(pb);
        glUniform4f(glGetUniformLocation(pb, "uTest"), 0, 0, 0, 0);
        glUniform4fv(loc, 14, &U[0][0][0]);
        glClear(GL_COLOR_BUFFER_BIT);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 32, &vb[0][0]);
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, &vb[0][6]);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, &vb[0][4]);
        glDrawElements(GL_TRIANGLES, 12, GL_UNSIGNED_SHORT, kIx);
        CHECK(px_at(100, 240) == RED && px_at(540, 240) != RED, "batched parts: slot 0's part drawn, slot 1's put away: %08x %08x", px_at(100, 240), px_at(540, 240));
        memset(U[0][4], 0, 16);
        U[1][4][0] = U[1][4][1] = U[1][4][2] = U[1][4][3] = 1;
        glUniform4fv(loc, 14, &U[0][0][0]);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawElements(GL_TRIANGLES, 12, GL_UNSIGNED_SHORT, kIx);
        CHECK(px_at(100, 240) != RED && px_at(540, 240) == RED, "batched parts: the other way round: %08x %08x", px_at(100, 240), px_at(540, 240));
    }

    /* 8. the swap: the picture on the window, the right way up (red on top), and the callback */
    {
        V w[6];
        glUseProgram(p);
        glUniform4f(glGetUniformLocation(p, "uMode"), 0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        quad(w, -1, 0, 1, 1, 0x80000080u);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glDisableVertexAttribArray(2);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(V), &w[0].x);
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(V), &w[0].c[0]);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        vglSwapBuffers(GL_FALSE);
        CHECK(sCbCalls == 1, "display callback called once: %d", sCbCalls);
        realBindFb(GL_FRAMEBUFFER, 0); /* (the window's own, read back: SDL keeps the picture in the back buffer after a swap on Mesa, so draw again) */
    }
    /* 8b. a render thread that keeps the context while it waits for work (vita_vgl.c rt_thread): asked for it by
       the wake hook, it lets go; the drawing thread then gets it at once, not after the 3 s give-up */
    {
        pthread_t rt;
        unsigned long long t0;
        sRtSema = sceKernelCreateSema("rt", 0, 0, 100, NULL);
        gR36CtxWake = rt_wake;
        pthread_create(&rt, NULL, rt_main, NULL);
        while (!sRtHas) {
            sceKernelDelayThread(1000); /* (this thread lets go of the context while it waits) */
        }
        t0 = R36_NowNs();
        glClear(GL_COLOR_BUFFER_BIT); /* (the render thread asleep with the context: woken to let go) */
        CHECK(R36_NowNs() - t0 < 500000000ull, "the context back from the sleeping render thread in %llu ms", (R36_NowNs() - t0) / 1000000ull);
        sRtQuit = 1;
        sceKernelSignalSema(sRtSema, 1);
        pthread_join(rt, NULL);
        gR36CtxWake = NULL;
        CHECK(glGetError() == GL_NO_ERROR, "no GL error after the render thread");
    }
    sDrawerReady = 1;
    /* 9. idle as the drawing thread is between lists: another thread prints on the text screen */
    while (!sTextDone) {
        sceKernelDelayThread(200);
    }
    /* and the context is back with this thread */
    glClear(GL_COLOR_BUFFER_BIT);
    CHECK(glGetError() == GL_NO_ERROR, "no GL error at the end");
    return NULL;
}

int main(void) {
    pthread_t th;
    setenv("R36_VSYNC", "0", 1);
    psvDebugScreenInit();
    psvDebugScreenPrintf("\e[33mR36S GL test\e[39m\n");
    pthread_create(&th, NULL, drawer, NULL);
    while (!sDrawerReady) {
        usleep(1000);
    }
    psvDebugScreenPrintf("printed while the drawing thread holds the context\n");
    sTextDone = 1;
    pthread_join(th, NULL);
    printf(sFail ? "\n%d FAILED\n" : "\nall passed\n", sFail);
    return sFail != 0;
}
