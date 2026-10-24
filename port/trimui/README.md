# TrimUI Smart Pro (stock system, PortMaster)

A 64-bit (aarch64) build of the R36S port (`port/r36s`) for the TrimUI Smart Pro: Allwinner A133 Plus, 4× Cortex-A53
up to 2.0 GHz, PowerVR GE8300 (OpenGL ES 3.2), 1 GB, 1280×720, kernel 4.9, glibc 2.33, SDL 2.30 (`mali` video).
`probe/` is the program that checked all of that on the device.

    BT3_PLATFORM=tsp python3 port/r36s/build.py            # compile, link, package
    -> port/build/tsp/Tenkaichi3Decomp-TrimUI.zip          # Roms/PORTS/<name>.sh + Data/ports/bt3/

## 64 bits

The game's code keeps 4-byte pointers: each game file goes through `port/tools/ptr32.py` (pointers become
`* __ptr32 __uptr`), `srcfix.shifts`, LLVM IR and `irfix.py` (calls through 4-byte function pointers), as the
decomp's own PC 64-bit build does. Port files without game text are compiled as they are. So everything the game can
point at lies below 4 GB:

- the program: linked non-PIE at 0x1000000;
- the C library's heap: the break area after the program (`mallopt(M_MMAP_MAX, 0)`, `M_ARENA_MAX 1`, and
  `MALLOC_ARENA_MAX=1` in the launcher); the game heap and memory blocks come from it;
- every thread's stack (the game keeps addresses of locals): `__wrap_pthread_create` in `shim/r36s_psp2.c` maps
  them from 0xC0000000 up and reuses them; the game itself runs on the `bt3_game` thread.

The log warns (`tsp: ... above 4 GB`) if a stack or a memory block ever lands higher.

## Defaults

16:9 (`wide=1`), fights and menus drawn at 854×480 (`bt3.txt`: `res=`, `menures=`), the work targets' effects and
the glow off (`fxwt=1`, `fxglow=1`), the processor at 1608 MHz with the `performance` governor while the game runs.
`launcher.txt`, `log.txt`, `crash.txt` (aarch64 registers and the calls on the stack) are written next to the
program; `prof.txt` in the test builds (the release launcher sets `R36_PROF=0`).

## Credits

arielxflores (the PS Vita port this runs), z3xox and contributors (Tenkaichi3Decomp), the BT3-Decompiled authors;
the TrimUI port by monseyo521. License: AGPL-3.0 (`LICENSE`), the earlier code's MIT notice in `LICENSE-MIT`.