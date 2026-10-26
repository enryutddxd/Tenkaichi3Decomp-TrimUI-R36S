# port/r36s

**The 64-bit version (v1.1, the one released):** `BT3_PLATFORM=r36s64 python3 port/r36s/build.py` (add
`BT3_RELEASE=1` for the release package, without the measurements) gives `port/build/r36s64/`, the `ports/bt3_64`
folder and its launcher. It is the TrimUI Smart Pro's build (`BT3_PLATFORM=tsp`) for the R36S's Mali-G31: uniform
blocks in a persistently mapped stream, draws given their index range (the Mali driver's memory), the swap on a
thread of its own (made current through SDL on KMSDRM), the CPU held at 1200 MHz. The player's README is
`package/README_64.txt`; what was measured is in the commit history (tests 1 to 33). The rest of this file is
about the first, 32-bit version.

The R36S version of the port: the PS Vita version (port/vita) built for the RK3326 handhelds (R36S and similar) on
ArkOS / dArkOSRE, a 32-bit ARM Linux program on SDL2 and OpenGL ES 3 (the Mali-G31).

The game's code and the Vita's own sources are compiled as they are (the few R36S changes in them are inside
`#ifdef PORT_R36S`); underneath them, `shim/` stands in for what the Vita had:

| File | What it is |
|---|---|
| `shim/r36s_psp2.c` | The Vita's system calls on Linux: threads (with the Vita's core masks), semaphores, files (`ux0:data/bt3` is the program's folder), the controller (SDL game controller), the 59.94 Hz vertical blank, sound (SDL audio), a crash report (`crash.txt`) |
| `shim/r36s_gl.c` | vitaGL on OpenGL ES 3: the display as three framebuffer objects drawn upside down as vitaGL keeps it in memory, texture names, mapped buffers, the fixed-function pipeline, the renderer's own extensions; the GL context handed between threads |
| `shim/r36s_cg.c` | The renderer's Cg shaders translated to GLSL ES 3.00 |
| `shim/r36s_text.c` | The text screen (errors) |
| `shim/psp2/`, `vitaGL.h`, ... | The headers the Vita's sources include |
| `package/` | The launcher for ArkOS / PortMaster and the player's README |
| `tests/shader_check.py` | Every shader of vita_vgl.c translated and checked with glslangValidator |
| `tests/gl_test.sh` | The GL layer on the build machine (Mesa in Xvfb), pixels read back |
| `shim/r36s_prof.c` | prof.txt: a sampling profiler per thread (every 10 s), with the system's counts |
| `HANDOFF.md` | The state of the work, what was tried and measured, what is left |

## Building

On Linux (x86-64), no SDL2, GLES or ARM sysroot needed:

```sh
pip install ziglang libclang            # the compiler (glibc 2.28 for armhf) and the bindings ptr32.py uses
sudo apt install binutils-arm-linux-gnueabihf gcc-arm-linux-gnueabihf zip   # (GCC: the renderer's files, see build.py; BT3_GCC=0 without it)
python3 port/r36s/build.py              # compile, link, package
```

The result is `port/build/r36s/Tenkaichi3Decomp-R36S.zip` (the `ports/` folder of ArkOS) and
`port/build/r36s/bt3.unstripped`, kept for reading `crash.txt` (`arm-linux-gnueabihf-addr2line -f -e bt3.unstripped <pc>`).
`BT3_INCREMENTAL=1` reuses objects newer than their source.

## Differences from the Vita build

- Pictures at 640 x 480 (the screen's own), 4:3; `res=WxH` in bt3.txt for the fights.
- Textures with a palette are converted to colours (OpenGL ES has no palette textures).
- The draws are made on the drawing thread itself (the Vita's separate render thread: bt3.txt rt=1, see HANDOFF.md).
- The direct path's model parts are drawn several to a draw (gl_VertexID slots; R36_BATCH=0: one by one).
- The picture is not handed to the GPU in parts (vglSplitScene); the renderer reads the display through its copies.
- No touch panels: L2 / R2 are the R36S's own; L3 / R3 the sticks pressed, or SELECT + L1 / R1.
