/* R36S: forced into the Vita's own sources (port/vita) and the PC renderer and sound files when built for the R36S
   (port/r36s/build.py). */
#ifndef R36S_FORCE_H
#define R36S_FORCE_H
#include "psp2_shim.h"
/* The Vita's stand-in for the dozen SDL3 sound calls (port/vita/sdlshim, port/vita/vita_audio.c) has names that SDL2
   has too, with other meanings; in this program SDL2 is the real one (port/r36s/shim), so the stand-in's are renamed. */
#define SDL_InitSubSystem R36V_SDL_InitSubSystem
#define SDL_OpenAudioDevice R36V_SDL_OpenAudioDevice
#define SDL_ResumeAudioDevice R36V_SDL_ResumeAudioDevice
#define SDL_GetCurrentAudioDriver R36V_SDL_GetCurrentAudioDriver
#define SDL_GetError R36V_SDL_GetError
#define SDL_CreateAudioStream R36V_SDL_CreateAudioStream
#define SDL_SetAudioStreamGetCallback R36V_SDL_SetAudioStreamGetCallback
#define SDL_BindAudioStream R36V_SDL_BindAudioStream
#define SDL_PutAudioStreamData R36V_SDL_PutAudioStreamData
#define SDL_SetAudioStreamGain R36V_SDL_SetAudioStreamGain
#define SDL_LockAudioStream R36V_SDL_LockAudioStream
#define SDL_UnlockAudioStream R36V_SDL_UnlockAudioStream
#define SDL_ClearAudioStream R36V_SDL_ClearAudioStream
#define SDL_DestroyAudioStream R36V_SDL_DestroyAudioStream
#endif
