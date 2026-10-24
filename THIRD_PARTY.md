# Third-party code and data

## In the Vita program (linked into `eboot.bin`)

| Component | Use | License |
|---|---|---|
| [vitaGL](https://github.com/Rinnegatamante/vitaGL) by Rinnegatamante, with this port's changes (`port/vita/vitagl/vitagl.patch`) | OpenGL-style rendering on the Vita's GPU | LGPL-3.0 |
| [vitaShaRK](https://github.com/Rinnegatamante/vitaShaRK) by Rinnegatamante | Shader compilation at run time (for vitaGL) | LGPL-3.0 |
| [SceShaccCgExt](https://github.com/bythos14/SceShaccCgExt) by bythos14 | Extension library for the system's shader compiler (for vitaShaRK) | GPL-3.0 |
| [math-neon](https://github.com/vitasdk/packages/tree/master/math-neon) by Lachlan Tychsen-Smith | NEON maths routines (for vitaGL) | MIT |
| [zlib](https://zlib.net/) by Jean-loup Gailly and Mark Adler | Compression | zlib |
| `debugScreen.c` from [vitasdk/samples](https://github.com/vitasdk/samples) | The text screen for start-up messages and errors | CC0 1.0 |
| newlib (C library) and the [VitaSDK](https://vitasdk.org/) system stubs | C library, system calls | BSD-style / MIT |
| `port/third_party/newlib_libm` | The PS2's single-precision maths routines (from newlib), as in the PC port | newlib's BSD-style terms, `port/third_party/newlib_libm/COPYING.NEWLIB` |

`libshacccg.suprx`, the system's shader compiler, is not included: each user extracts it from their own Vita.

## In the R36S program (`port/r36s`)

| Component | Use | License |
|---|---|---|
| [SDL2](https://www.libsdl.org/) headers (`port/r36s/third_party/SDL2`); the library itself is the system's | Window, OpenGL ES context, controller, sound | zlib |
| Khronos OpenGL ES 3 headers (`port/r36s/third_party/khronos`) | The OpenGL ES API | MIT / Apache-2.0 (Khronos) |
| The PSP SDK's 8 x 8 debug font (`port/r36s/third_party/r36s_font.h`, from vitasdk/samples' `debugScreenFont.c`) | The text screen | BSD (PSPSDK) |
| [zig](https://ziglang.org/) (build time) | The C compiler and glibc 2.28 stubs for armhf | MIT |

## In the TrimUI Smart Pro program (`port/r36s` built for aarch64, `port/trimui`)

The same as the R36S program's (SDL2 headers, Khronos headers, the PSP SDK's debug font; SDL2, EGL and the
PowerVR OpenGL ES driver are the system's own, not included), with [zig](https://ziglang.org/) at build time for
aarch64 and glibc 2.33. `port/tools/irfix.py` (an LLVM IR fix-up) is this port's own.

## Used at build time only

| Component | Use | License |
|---|---|---|
| [libclang](https://clang.llvm.org/) | Rewriting the game's call arguments for the PS2 compiler's evaluation order (`port/tools/ptr32.py`, from Tenkaichi3Decomp) | Apache-2.0 with LLVM exception |
| [DejaVu Sans Bold](https://dejavu-fonts.github.io/) | The lettering of the new labels on the options screen's controller page (`port/vita/tools/labelart.py`; the result is in `port/vita/vita_optart.h`) | Bitstream Vera / DejaVu font license |

## Not included, used when present on the Vita

| Component | Use | License |
|---|---|---|
| [CapUnlocker](https://github.com/GrapheneCt/CapUnlocker) by GrapheneCt | Lets the game's threads use the fourth CPU core | MIT |
| [libperf](https://github.com/shoui520/libperf) | Processor event counters in the development log (`port/vita/vita_perf.c`) | see its repository |

## From Tenkaichi3Decomp

Everything outside `port/vita/` comes from [Tenkaichi3Decomp](https://github.com/z3xox/Tenkaichi3Decomp) at
commit `dd6b20c`, with the changes the Vita needed (most of them inside `#ifdef PORT_VITA`). Its own third-party
code (Dear ImGui in `port/third_party/imgui`, MIT) is used by the PC build only. See
[docs/port/README-PC.md](docs/port/README-PC.md).
