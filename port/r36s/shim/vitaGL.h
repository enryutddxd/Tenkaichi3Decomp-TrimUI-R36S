/*
 * R36S: vitaGL as port/vita/vita_vgl.c knows it, on OpenGL ES 3 (port/r36s/shim/r36s_gl.c).
 *
 * The GL names the Vita's renderer calls are macros for the shim's functions (r36_gl...), so that nothing of the
 * program can be taken for the system's OpenGL ES library's own entry points; vitaGL's own calls (vgl...) and the
 * names of its internals the renderer reads (the display buffers) are declared here.
 */
#ifndef R36S_VITAGL_H
#define R36S_VITAGL_H
#include <stdint.h>
#include <stddef.h>
#define GL_GLES_PROTOTYPES 0
#include <GLES3/gl3.h>
#include "psp2_shim.h"
#include "psp2_gxm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* what OpenGL ES 3 does not have and vitaGL does */
#define GL_ALPHA_TEST 0x0BC0
#define GL_TEXTURE_ENV 0x2300
#define GL_TEXTURE_ENV_MODE 0x2200
#define GL_MODULATE 0x2100
#define GL_DECAL 0x2101
#define GL_VERTEX_ARRAY 0x8074
#define GL_COLOR_ARRAY 0x8076
#define GL_TEXTURE_COORD_ARRAY 0x8078
#define GL_MODELVIEW 0x1700
#define GL_PROJECTION 0x1701
#ifndef GL_WRITE_ONLY
#define GL_WRITE_ONLY 0x88B9
#endif
#define GL_CG_VERTEX_SHADER_EXT 0x8B31F
#define GL_CG_FRAGMENT_SHADER_EXT 0x8B30F

#define R36_GL_PASS(r, n, p, a) r r36_##n p;
#define R36_GL_WRAP(r, n, p, a) r r36_##n p;
#include "r36s_glfuncs.h"
#define glActiveTexture r36_glActiveTexture
#define glAttachShader r36_glAttachShader
#define glBindAttribLocation r36_glBindAttribLocation
#define glBlendEquation r36_glBlendEquation
#define glBlendEquationSeparate r36_glBlendEquationSeparate
#define glBlendFunc r36_glBlendFunc
#define glBlendFuncSeparate r36_glBlendFuncSeparate
#define glClearColor r36_glClearColor
#define glClearDepthf r36_glClearDepthf
#define glClearStencil r36_glClearStencil
#define glColorMask r36_glColorMask
#define glCompileShader r36_glCompileShader
#define glCreateProgram r36_glCreateProgram
#define glDepthFunc r36_glDepthFunc
#define glDepthMask r36_glDepthMask
#define glDeleteFramebuffers r36_glDeleteFramebuffers
#define glFinish r36_glFinish
#define glFlush r36_glFlush
#define glGenFramebuffers r36_glGenFramebuffers
#define glGetShaderInfoLog r36_glGetShaderInfoLog
#define glGetShaderiv r36_glGetShaderiv
#define glGetProgramiv r36_glGetProgramiv
#define glGetProgramInfoLog r36_glGetProgramInfoLog
#define glGetUniformLocation r36_glGetUniformLocation
#define glStencilFunc r36_glStencilFunc
#define glStencilMask r36_glStencilMask
#define glStencilOp r36_glStencilOp
#define glUniform1i r36_glUniform1i
#define glUniform1f r36_glUniform1f
#define glUniform4f r36_glUniform4f
#define glUniform4fv r36_glUniform4fv
#define glGenBuffers r36_glGenBuffers
#define glGetError r36_glGetError
#define glCheckFramebufferStatus r36_glCheckFramebufferStatus
#define glLinkProgram r36_glLinkProgram
#define glUseProgram r36_glUseProgram
#define glEnableVertexAttribArray r36_glEnableVertexAttribArray
#define glDisableVertexAttribArray r36_glDisableVertexAttribArray
#define glVertexAttribPointer r36_glVertexAttribPointer
#define glBindBuffer r36_glBindBuffer
#define glBufferData r36_glBufferData
#define glDeleteBuffers r36_glDeleteBuffers
#define glMapBuffer r36_glMapBuffer
#define glUnmapBuffer r36_glUnmapBuffer
#define glDrawArrays r36_glDrawArrays
#define glDrawElements r36_glDrawElements
#define glGenTextures r36_glGenTextures
#define glDeleteTextures r36_glDeleteTextures
#define glBindTexture r36_glBindTexture
#define glTexImage2D r36_glTexImage2D
#define glTexSubImage2D r36_glTexSubImage2D
#define glTexParameteri r36_glTexParameteri
#define glBindFramebuffer r36_glBindFramebuffer
#define glFramebufferTexture2D r36_glFramebufferTexture2D
#define glViewport r36_glViewport
#define glScissor r36_glScissor
#define glClear r36_glClear
#define glEnable r36_glEnable
#define glDisable r36_glDisable
#define glCreateShader r36_glCreateShader
#define glShaderSource r36_glShaderSource
#define glShaderBinary r36_glShaderBinary
#define glAlphaFunc r36_glAlphaFunc
#define glColor4f r36_glColor4f
#define glVertexPointer r36_glVertexPointer
#define glColorPointer r36_glColorPointer
#define glTexCoordPointer r36_glTexCoordPointer
#define glEnableClientState r36_glEnableClientState
#define glDisableClientState r36_glDisableClientState
#define glTexEnvi r36_glTexEnvi
#define glMatrixMode r36_glMatrixMode
#define glLoadIdentity r36_glLoadIdentity

/* ---- vitaGL's own ---- */
typedef enum vglMemType { VGL_MEM_VRAM = 0, VGL_MEM_RAM = 1, VGL_MEM_SLOW = 2, VGL_MEM_BUDGET = 3, VGL_MEM_EXTERNAL = 4, VGL_MEM_ALL = 5 } vglMemType;
GLboolean vglInitExtended(int legacy_pool_size, int width, int height, int ram_threshold, SceGxmMultisampleMode msaa);
void vglSwapBuffers(GLboolean has_commondialog);
void vglWaitVblankStart(GLboolean enable);
void vglSetDisplayCallback(void (*cb)(void *framebuf));
void vglSetupScratchMemory(GLboolean vert, GLboolean frag);
void vglSetVertexBufferSize(uint32_t size);
void vglSetVDMBufferSize(uint32_t size);
void vglSetFragmentBufferSize(uint32_t size);
void vglSetParamBufferSize(uint32_t size);
void vglSetupRenderTargetScenesNum(uint8_t size_main, uint8_t size_fbos);
void vglSwapResolution(int width, int height);
size_t vglMemFree(vglMemType type);
void vglGetShaderBinary(GLuint index, GLsizei bufSize, GLsizei *length, void *binary);
void vglSetupTexture(GLenum target, void *data, SceGxmTextureFormat format, SceGxmTextureType type, GLsizei width, GLsizei height, GLsizei mip_count);
void vglOverloadTexDataPointer(GLenum target, void *data);
void *vglGetTexDataPointer(GLenum target);
SceGxmTexture *vglGetGxmTexture(GLenum target);
void vglTexImageDepthBuffer(GLenum target);
void vglLazyFree(void *addr);
void *vglAllocFromScratch(size_t size);
GLboolean vglSplitScene(void);
void vglBt3ContNext(void);
void vglBt3Dirty(void);
GLboolean vglFastPrepare(GLenum type);
void vglFastDraw(GLenum type, uint32_t offset, uint32_t count, const void *vunif, uint32_t size);
void vglFastDrawPtr(GLenum type, const void *idx, uint32_t count, const void *vunif, uint32_t size);
uint32_t vglFastVertUniformSize(GLuint prog);
int vglFastUniformOffset(GLuint prog, GLint location);
void *gpu_alloc_mapped_aligned_for_gpu(size_t alignment, size_t size);

/* (the R36S only) a range of a buffer mapped with glMapBuffer was written: given to OpenGL ES before the next draw */
void vglR36Dirty(const void *at, size_t size);

extern void *gxm_color_surfaces_addr[];
extern unsigned int gxm_back_buffer_index;
extern unsigned int gxm_front_buffer_index;
extern SceGxmContext *gxm_context;
extern int vgl_bt3_split_on;
extern unsigned vgl_bt3_splits;
extern uint32_t vgl_flip_hist[5];
extern uint64_t vgl_dbg_time[8];

#ifdef __cplusplus
}
#endif
#endif
