/* R36S: the few sceGxm names port/vita/vita_vgl.c uses around vitaGL's textures (all without effect here: the
   textures they would change are OpenGL ES's own, see r36s_gl.c). */
#ifndef R36S_PSP2_GXM_H
#define R36S_PSP2_GXM_H
#include <stdint.h>

typedef struct SceGxmTexture { uint32_t controlWords[4]; } SceGxmTexture;
typedef struct SceGxmContext SceGxmContext;
typedef enum SceGxmTextureFormat {
    SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR = 0x0C000000,
    SCE_GXM_TEXTURE_FORMAT_P8_ABGR = 0x95000000,
    SCE_GXM_TEXTURE_FORMAT_DF32M = 0x1A044000,
    SCE_GXM_TEXTURE_FORMAT_U8_R111 = 0x00004000
} SceGxmTextureFormat;
typedef enum SceGxmTextureType {
    SCE_GXM_TEXTURE_SWIZZLED = 0x00000000,
    SCE_GXM_TEXTURE_CUBE = 0x40000000,
    SCE_GXM_TEXTURE_LINEAR = 0x60000000,
    SCE_GXM_TEXTURE_TILED = 0x80000000,
    SCE_GXM_TEXTURE_LINEAR_STRIDED = 0xC0000000
} SceGxmTextureType;
typedef enum SceGxmTextureFilter { SCE_GXM_TEXTURE_FILTER_POINT = 0, SCE_GXM_TEXTURE_FILTER_LINEAR = 1 } SceGxmTextureFilter;
typedef enum SceGxmTextureAddrMode { SCE_GXM_TEXTURE_ADDR_REPEAT = 0, SCE_GXM_TEXTURE_ADDR_CLAMP = 1 } SceGxmTextureAddrMode;
typedef enum SceGxmMultisampleMode { SCE_GXM_MULTISAMPLE_NONE = 0, SCE_GXM_MULTISAMPLE_2X = 1, SCE_GXM_MULTISAMPLE_4X = 2 } SceGxmMultisampleMode;

int sceGxmTextureInitLinear(SceGxmTexture *texture, const void *data, SceGxmTextureFormat texFormat, unsigned int width, unsigned int height, unsigned int mipCount);
int sceGxmTextureInitSwizzled(SceGxmTexture *texture, const void *data, SceGxmTextureFormat texFormat, unsigned int width, unsigned int height, unsigned int mipCount);
int sceGxmTextureSetMinFilter(SceGxmTexture *texture, SceGxmTextureFilter minFilter);
int sceGxmTextureSetMagFilter(SceGxmTexture *texture, SceGxmTextureFilter magFilter);
int sceGxmTextureSetUAddrMode(SceGxmTexture *texture, SceGxmTextureAddrMode mode);
int sceGxmTextureSetVAddrMode(SceGxmTexture *texture, SceGxmTextureAddrMode mode);
SceGxmTextureFilter sceGxmTextureGetMinFilter(const SceGxmTexture *texture);
SceGxmTextureFilter sceGxmTextureGetMagFilter(const SceGxmTexture *texture);
SceGxmTextureAddrMode sceGxmTextureGetUAddrMode(const SceGxmTexture *texture);
SceGxmTextureAddrMode sceGxmTextureGetVAddrMode(const SceGxmTexture *texture);
SceGxmTextureFormat sceGxmTextureGetFormat(const SceGxmTexture *texture);
void *sceGxmTextureGetData(const SceGxmTexture *texture);
int sceGxmFinish(SceGxmContext *context);
#endif
