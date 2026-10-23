# port/r36s

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

## Building

On Linux (x86-64), no SDL2, GLES or ARM sysroot needed:

```sh
pip install ziglang libclang            # the compiler (glibc 2.28 for armhf) and the bindings ptr32.py uses
sudo apt install binutils-arm-linux-gnueabihf zip
python3 port/r36s/build.py              # compile, link, package
```

The result is `port/build/r36s/Tenkaichi3Decomp-R36S.zip` (the `ports/` folder of ArkOS) and
`port/build/r36s/bt3.unstripped`, kept for reading `crash.txt` (`arm-linux-gnueabihf-addr2line -f -e bt3.unstripped <pc>`).
`BT3_INCREMENTAL=1` reuses objects newer than their source.

## Differences from the Vita build

- Pictures at 640 x 480 (the screen's own), 4:3; `res=WxH` in bt3.txt for the fights.
- Textures with a palette are converted to colours (OpenGL ES has no palette textures).
- The draws are made on the drawing thread itself (the Vita's separate render thread is off: a GL context belongs to
  one thread).
- The picture is not handed to the GPU in parts (vglSplitScene); the renderer reads the display through its copies.
- No touch panels: L2 / R2 are the R36S's own; L3 / R3 the sticks pressed, or SELECT + L1 / R1.
