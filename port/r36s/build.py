#!/usr/bin/env python3
"""R36S (and other RK3326 handhelds running ArkOS / dArkOSRE) build of Tenkaichi3Decomp.

    python3 port/r36s/build.py [compile] [link] [package]       (no step named: all three)

The R36S is a 32-bit ARM Linux target as far as this build is concerned (ArkOS ships armhf libraries next to its
aarch64 ones, and the game's code needs 4-byte pointers, as on the PS2 and the Vita). So this build is the Vita
build (port/vita/build.py) with another toolchain underneath:
  - zig's clang for armhf Linux with glibc 2.28 (pip install ziglang), so that the program starts on ArkOS's old
    glibc whatever machine builds it;
  - binutils-arm-linux-gnueabihf for the data tables (as) and the renames (objcopy);
  - the Vita's own sources (port/vita) for everything they share with this build, on a layer of the Vita's system
    calls and of vitaGL written for Linux, SDL2 and OpenGL ES 3 (port/r36s/shim).
Nothing here needs SDL2 or GLES installed: the program loads SDL2 at start (ArkOS's own) and takes every GL entry
point from it (SDL_GL_GetProcAddress). For linking, a stub libSDL2-2.0.so.0 with the names used is generated.

Environment:
    BT3_R36S_ZIG       the command for zig (default: "python3 -m ziglang")
    BT3_R36S_BINUTILS  the prefix of the ARM binutils (default: arm-linux-gnueabihf-)
    BT3_INCREMENTAL=1  reuse objects newer than their source (headers are not tracked)
    BT3_JOBS=<n>       parallel compiles
"""
import concurrent.futures, os, pathlib, re, shlex, shutil, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/tools"))
sys.path.insert(0, str(ROOT / "port/vita"))
import portsrc, eeconst

R = ROOT / "port/r36s"
V = ROOT / "port/vita"
# BT3_PLATFORM=tsp: the TrimUI Smart Pro (stock firmware, through PortMaster): a 64-bit ARM program (its GPU driver is
# 64-bit only), with the game's pointers kept 4 bytes wide as in the PC's 64-bit build (port/tools/ptr32.py, irfix.py:
# every pointer of the game's text is `* __ptr32 __uptr`, all game memory below 4 GB). See port/trimui/README.md.
PLAT = os.environ.get("BT3_PLATFORM", "r36s")  # r36s (32-bit ARM), r36s64 (the R36S as a 64-bit program, as the TrimUI's), tsp
TSP = PLAT == "tsp"
A64 = PLAT in ("tsp", "r36s64")  # (a 64-bit program: the game's pointers kept 4 bytes wide, see below)
OBJ = ROOT / f"port/build/obj_{PLAT}"
OUT = ROOT / f"port/build/{PLAT}"
GEN = ROOT / f"port/build/gen/{PLAT}"
ZIG = shlex.split(os.environ.get("BT3_R36S_ZIG", f"{shlex.quote(sys.executable)} -m ziglang"))
BU = os.environ.get("BT3_R36S_BINUTILS", "aarch64-linux-gnu-" if A64 else "arm-linux-gnueabihf-")
TARGET = ["-target", "aarch64-linux-gnu.2.33", "-mcpu=cortex_a53"] if TSP else \
         ["-target", "aarch64-linux-gnu.2.28", "-mcpu=cortex_a35"] if A64 else ["-target", "arm-linux-gnueabihf.2.28", "-mcpu=cortex_a35-neon"]
CC = ZIG + ["cc"] + TARGET
CXX = ZIG + ["c++"] + TARGET

# (as port/vita/build.py)
X_DEFS = ["addsf3", "subsf3", "mulsf3", "divsf3", "negsf2", "eqsf2", "nesf2", "ltsf2", "lesf2", "gtsf2", "gesf2", "unordsf2",
          "fixsfsi", "fixunssfsi", "floatsisf", "floatunsisf"]
RENAME = {f"__{n}": f"PortX_{n}" for n in X_DEFS}
RENAME |= {n: "PortNL_" + n.lstrip("_") for n in ["__ieee754_acosf", "__ieee754_asinf", "__ieee754_atan2f", "__ieee754_powf",
           "__ieee754_rem_pio2f", "__ieee754_sqrtf", "__kernel_cosf", "__kernel_rem_pio2f", "__kernel_sinf", "__kernel_tanf"]}
RENAME |= {"_ctype_": "PortPS2_ctype_"}
RENAME_SOFTFLOAT = {"RefVu0_AddBits": "PortX_VuAdd", "RefVu0_MulBits": "PortX_VuMul", "RefVu0_DivBits": "PortX_VuDiv"}

ABI = ["-fsigned-char", "-fno-short-enums", "-fsigned-bitfields"]
# The game's float arithmetic follows the PS2 by running the float unit in round-toward-zero / flush-to-zero
# (Vita_FloatMode). NEON's float instructions ignore that mode, so nothing of the game's is vectorised.
NOVEC = ["-fno-vectorize", "-fno-slp-vectorize"]
# No NEON in what is built from here (but where a file asks for it): the compiler gives NEON loads and stores of a
# 16-byte type an alignment of 16 that the R36S's processor enforces (an alignment fault, SIGBUS), and the game's data
# (PS2 vectors and the structures around them) is not always laid out that way. The float unit (VFPv4) alone needs 4.
FPU = []  # (the -neon of TARGET's -mcpu: zig takes no -mfpu)
NEON_FILES = {"vita_vgl.c"}  # (arm_neon.h intrinsics)
COMMON = FPU + ["-O2", "-g", "-fno-strict-aliasing", "-ffp-contract=off", "-w", "-Wno-error", "-fno-pie", "-fcommon",
          "-Wno-error=int-conversion", "-Wno-error=incompatible-function-pointer-types", "-Wno-error=implicit-function-declaration",
          "-Wno-error=incompatible-pointer-types", "-Wno-error=return-type", "-Wno-error=int-to-pointer-cast",
          "-DPORT_VITA=1", "-DPORT_VITA_FIXED=1", "-DPORT_R36S=1", '-DPORT_VITA_SFX=".txt"', "-DPORT_VITA_GFX=1", "-DPORT_VITA_VGL=1",
          "-DMPEG2_PLAYER=1", "-DPORT_VITA_SOUND=1", "-DPORT_VITA_NATIVE=1", "-DPORT_VITA_RELEASE=1"] + \
         (["-DPORT_TSP=1", "-DPORT_A64=1", "-fms-extensions"] if TSP else ["-DPORT_R36S64=1", "-DPORT_A64=1", "-fms-extensions"] if A64 else [])
# (the 64-bit build's parse of the preprocessed text: ptr32.transform, srcfix.shifts)
P64_ARGS = ["-x", "cpp-output", "--target=aarch64-linux-gnu", "-std=gnu99", "-fsigned-char", "-fno-short-enums", "-fms-extensions", "-w",
            "-Wno-error=return-type", "-Wno-error=return-mismatch", "-Wno-error=int-conversion", "-Wno-error=incompatible-pointer-types",
            "-Wno-error=implicit-function-declaration", "-Wno-error=incompatible-function-pointer-types", "-Wno-error=int-to-pointer-cast"]
NOBUILTIN = ["-fno-builtin-" + n for n in "sinf cosf tanf asinf acosf atanf atan2f sqrtf powf floorf ceilf fabsf fmodf expf logf sin cos tan sqrt "
             "pow floor ceil fabs abs labs rand srand printf sprintf snprintf fprintf vsprintf puts putchar fputs fwrite strcpy strncpy strcat "
             "strncat strlen strcmp strncmp strchr strrchr strstr malloc calloc realloc free exit abort atoi memcmp memmove".split()]
SHIM_INC = [f"-I{R / 'shim'}", f"-I{R / 'third_party'}", f"-I{R / 'third_party/SDL2'}", f"-I{R / 'third_party/khronos'}"]
GAME = CC + ["-std=gnu89", "-c"] + COMMON + ABI + NOVEC + NOBUILTIN + \
       ["-include", "port_libm.h", "-Iinclude", "-Iport/include", "-Iport/vita"] + SHIM_INC + ["-include", "port_compat.h"]
R2L_ARGS = ["-x", "cpp-output", "--target=arm-none-eabi", "-mfloat-abi=soft", "-std=gnu89", "-fsigned-char", "-fno-short-enums", "-w",
            "-Wno-error=return-type", "-Wno-error=return-mismatch"]


def run(cmd, **kw):
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, **kw)


def r2l(text):
    import clang.cindex as ci  # (pip install libclang: its own library is found by itself)
    if not ci.Config.loaded and os.environ.get("BT3_LIBCLANG"):
        ci.Config.set_library_file(os.environ["BT3_LIBCLANG"])
    import ptr32, srcfix
    data, n = srcfix.shifts(ptr32.r2l(text.encode("latin-1"), R2L_ARGS), R2L_ARGS)
    return data.decode("latin-1"), n


def rename(r, o):
    if r.returncode == 0:
        syms = RENAME | (RENAME_SOFTFLOAT if o.name == "pc_softfloat_ps2.o" else {})
        m = OBJ / "rename" / (o.name + ".txt")
        m.parent.mkdir(exist_ok=True)
        m.write_text("".join(f"{a} {b}\n" for a, b in syms.items()))
        r2 = run([BU + "objcopy", f"--redefine-syms={m}", str(o)])
        if r2.returncode:
            return r2
    return r


def p64(text, i, game):
    """The 64-bit build's rewriting: the game's pointers 4 bytes wide (with its calls right to left), the PS2's shifts."""
    import clang.cindex as ci
    if not ci.Config.loaded and os.environ.get("BT3_LIBCLANG"):
        ci.Config.set_library_file(os.environ["BT3_LIBCLANG"])
    import ptr32, srcfix
    if not any(g for _, g in ptr32.origins(text.encode("latin-1"))):
        return text  # (no game text in it: nothing to rewrite)
    std = "-std=gnu89" if game else "-std=gnu99"
    args = [std if a == "-std=gnu99" else a for a in P64_ARGS]
    # (a type of the newer arm_neon.h the libclang of pip does not know: declared for the parse, taken out after)
    pre = "typedef unsigned char __mfp8;\n" if "__mfp8" in text else ""
    i.write_text(pre + text, encoding="latin-1")
    data = ptr32.transform(str(i), args).encode("latin-1")
    data, _ = srcfix.shifts(data, args)
    out = data.decode("latin-1")
    return out[len(pre):] if pre and out.startswith(pre) else out


def compile_ee(cmd, src, o, game=False, ee=True, p32=True):
    r = subprocess.run([a for a in cmd if a != "-c"] + ["-E", str(src)], cwd=ROOT, capture_output=True, text=True, encoding="latin-1")
    if r.returncode:
        return r
    i = pathlib.Path(str(o)[:-2] + ".i")
    text = eeconst.transform(r.stdout) if ee else r.stdout
    if A64 and p32:
        try:
            text = p64(text, i, game)
        except Exception as e:
            return subprocess.CompletedProcess(cmd, 1, "", f"error: ptr32: {e}\n")
    elif game:
        try:
            text, _ = r2l(text)
        except Exception as e:
            return subprocess.CompletedProcess(cmd, 1, "", f"error: r2l: {e}\n")
    i.write_text(text, encoding="latin-1")
    # The text is preprocessed already. zig's driver does not take a .i file for a versioned glibc target, so it is
    # given as C: without the forced includes and the macros (it would be preprocessed a second time).
    cc2, skip = [], False
    for a in cmd:
        if skip:
            skip = False
        elif a == "-include":
            skip = True
        elif not a.startswith("-D"):
            cc2.append(a)
    if A64:  # through LLVM IR: irfix.py repairs what the code generator cannot select for 4-byte pointers
        import irfix
        ll = pathlib.Path(str(o)[:-2] + ".ll")
        r = run([a for a in cc2 if a != "-c"] + ["-S", "-emit-llvm", "-x", "c", str(i), "-o", str(ll)])
        if r.returncode:
            return r
        fixed, _ = irfix.fix(ll.read_text(encoding="latin-1"), aarch64=True)
        ll.write_text(fixed, encoding="latin-1")
        opt = [a for a in cc2 if a.startswith("-O")][-1:] or ["-O2"]
        incs = [a for a in cc2 if a.startswith("-I")]  # (for the module's `.include "labels.inc"`)
        return rename(run(CC + ["-c", "-g"] + opt + incs + [f"-Wa,{a}" for a in incs] + [str(ll), "-o", str(o)]), o)
    return rename(run(cc2 + ["-x", "c", str(i), "-o", str(o)]), o)


def fresh(o, *srcs):
    return os.environ.get("BT3_INCREMENTAL") and o.exists() and all(o.stat().st_mtime > pathlib.Path(s).stat().st_mtime for s in srcs)


def cc_game(f):
    o = OBJ / (str(f.relative_to(ROOT / "src")).replace("/", "_")[:-2] + ".o")
    src, _, _ = portsrc.prepare(f)
    if fresh(o, src, f):
        return o, f, ""
    r = compile_ee(GAME + [f"-I{f.parent}"], src, o, game=True)
    return (o if r.returncode == 0 else None), f, r.stderr


def first_error(e):
    return next((l for l in e.splitlines() if "error" in l), e.strip()[:300])[:300]


def data_tables():
    """port/data/*.s for ARM (as the Vita build: a label at each table's first byte, and the table of where the values
    come from on the player's disc)."""
    GEN.mkdir(parents=True, exist_ok=True)
    where, checksum = {}, None
    for l in (ROOT / "port/data/index.txt").read_text().splitlines():
        p = l.split()
        if len(p) == 3 and not l.startswith("#"):
            where[p[0]] = (int(p[1]), int(p[2], 16))
        elif len(p) == 2 and p[0] == "checksum":
            checksum = int(p[1], 16)
    objs, decl, rows, total = [], [], [], 0
    names = sorted(where) + sorted(f.name[:-2] for f in (ROOT / "port/data").glob("*.s") if f.name[:-2] not in where)
    for name in names:
        which, so = where.get(name, (None, 0))
        label = "PortTbl_" + re.sub(r"\W", "_", name)
        text = (ROOT / "port/data" / (name + ".s")).read_text()
        off, lines = 0, []
        for l in text.splitlines():
            lines.append(l)
            p = l.split()
            if p and p[0] == ".p2align" and label not in "\n".join(lines):
                lines += [f".globl {label}", f"{label}:"]
            if not p or p[0].startswith("#"):
                continue
            if p[0] == ".space":
                if which is None:
                    raise SystemExit(f"{name}: values expected from the disc, but the table is not in port/data/index.txt")
                rows.append(f"    {{{label} + {off}, {which}, 0x{so + off:X}, {int(p[1])}}},")
                total += int(p[1])
                off += int(p[1])
            elif p[0] == ".zero":
                off += int(p[1])
            elif p[0] == ".long":
                off += 4
        decl.append(f"extern unsigned char {label}[];")
        s = GEN / (name + ".s")
        s.write_text("\n".join(lines) + "\n")
        o = OBJ / ("data_" + re.sub(r"\W", "_", name) + ".o")
        r = rename(run([BU + "as"] + ([] if A64 else ["-mfloat-abi=hard", "-mfpu=vfpv3"]) + [str(s), "-o", str(o)]), o)
        if r.returncode:
            print("FAILED", name, r.stderr.splitlines()[0][:200])
        else:
            objs.append(str(o))
    (GEN / "vita_dat.c").write_text(
        "/* generated by port/r36s/build.py from port/data: where the game's data tables get their values */\n"
        '#include "vita_dat.h"\n' + "\n".join(decl) + "\nconst PortDatRun gPortDatRuns[] = {\n" + "\n".join(rows) +
        f"\n}};\nconst unsigned gPortDatCount = {len(rows)};\nconst unsigned long long gPortDatChecksum = 0x{checksum:X}ull;\n")
    print(f"data tables: {len(objs)} files, {total} bytes in {len(rows)} runs to read from the disc")
    return objs


def port_sources():
    """(file, kind) as the Vita build's, with the R36S's own files (port/r36s) in place of the Vita's system."""
    S = ROOT / "port/src"
    out = [(ROOT / "src/port/vu0_a.c", "vu0"), (ROOT / "src/port/vu0_b.c", "vu0")]
    for n in ("gs_marker", "headless", "mathf_pc", "plat_gs", "plat_mc", "plat_movie", "plat_mpeg2",
              "plat_settings", "plat_stub", "plat_sys", "softfloat_ps2", "port_fast", "port_vufast", "tsp_state"):
        out.append((S / (n + ".c"), "gs" if n in ("plat_movie", "plat_mpeg2") else "ee"))
    out += [(V / "vita_mem.c", "hard"), (V / "vita_file.c", "hard"), (V / "vita_main.c", "hard"), (V / "vita_float.c", "hard"),
            (GEN / "vita_dat.c", "hard"), (S / "gs/gs_sony.c", "hard"),
            (V / "vita_gfx.c", "hard"), (S / "gs/gs_core.c", "gs"), (S / "gs/gs_vu1.c", "gs"),
            (V / "vita_perf.c", "hard"), (V / "vita_vgl.c", "hard"), (V / "vita_touch.c", "hard"), (V / "vita_optart.c", "hard"),
            (V / "vita_text.c", "hard"), (V / "vita_audio.c", "gs"), (S / "gs/snd_adx.c", "gs"), (S / "gs/snd_se.c", "gs"),
            (V / "vita_vufast_wrap.c", "hard")]
    out += [(f, "shim") for f in sorted((R / "shim").glob("*.c"))]
    return out


# The renderer and the PS2 graphics front end (kinds "gs" and "hard": the drawing thread's work) built with GCC, as
# the Vita's build does (-O3, its block reordering), for the R36S's Cortex-A35 with NEON: clang's code for them ran the
# drawing thread at ~50 ms a picture where the Vita's took ~19. Linked by zig as before (its glibc 2.28 stubs: the
# objects' C library calls get their versions there); GCC's C library headers are newer, so nothing that would pick
# newer entry points (fortify, C2x names) is on. BT3_GCC=0: clang for these too.
GCC = os.environ.get("BT3_R36S_GCC", BU + "gcc")
USE_GCC = os.environ.get("BT3_GCC", "1") != "0" and shutil.which(GCC) is not None and not A64
GCC_FLAGS = ["-U_TIME_BITS", "-U_FILE_OFFSET_BITS", "-mcpu=cortex-a35", "-mfpu=neon-fp-armv8", "-mfloat-abi=hard", "-marm", "-fno-pie", "-U_FORTIFY_SOURCE", "-D_FORTIFY_SOURCE=0",
             "-fno-stack-protector", "-fno-stack-clash-protection", "-freorder-blocks-and-partition"]
GCC_DROP = ("-Wno-error=incompatible-function-pointer-types", "-Wno-error=int-conversion", "-Wno-error=implicit-function-declaration",
            "-Wno-error=incompatible-pointer-types", "-Wno-error=return-type", "-Wno-error=int-to-pointer-cast", "-Wno-error")


def cc_port(item):
    f, kind = item
    o = OBJ / (("gs_" if kind == "gs" else "sh_" if kind == "shim" else "pc_") + f.stem + ".o")
    base = CC + ["-std=gnu99", "-c"] + COMMON + ["-Iinclude", "-Iport/include", "-Iport/src", "-Iport/vita"]
    if USE_GCC and kind in ("gs", "hard"):
        base = [GCC] + GCC_FLAGS + ["-std=gnu99", "-c"] + [a for a in COMMON if a not in GCC_DROP] + \
               ["-w", "-Iinclude", "-Iport/include", "-Iport/src", "-Iport/vita"]
        if f.name in ("vita_float.c",):  # (the float self-test: the float unit's own arithmetic, in the game's mode; NEON ignores the mode)
            base += ["-mfpu=vfpv4", "-fno-tree-vectorize", "-fno-tree-slp-vectorize"]
    if fresh(o, f):
        return o, f, ""
    if A64 and kind in ("gs", "hard", "vu0"):  # (through the 64-bit build's rewriting: the game's headers they include)
        if kind == "gs":
            cmd = base + ["-O3", "-include", "r36s_force.h", "-include", "vita_cold.h", "-Dgetenv=Vita_GsGetenv"] + SHIM_INC + \
                  [f"-I{ROOT / 'port/src/gs'}", f"-I{V / 'sdlshim'}"]
        elif kind == "hard":
            cmd = base + ["-O3", "-include", "r36s_force.h", "-include", "vita_cold.h"] + SHIM_INC + [f"-I{GEN}"]
        else:
            fast = ["-include", "vita_vu0_inline.h", "-DRefVu0_AddBits=VitaVu_AddBits", "-DRefVu0_MulBits=VitaVu_MulBits", "-DRefVu0_DivBits=VitaVu_DivBits"]
            if f.name == "vu0_b.c":
                fast += [f"-DRefVu0_{n}=VitaVuF_{n}" for n in ("Add", "Sub", "Mul", "Div")]
            cmd = base + ABI + NOVEC + ["-O3"] + fast + ["-include", "math.h", "-include", "stdlib.h", "-include", "port_libm.h",
                                                       "-include", "vu0_names.h", "-DREF_VU0_EXTERN_ARITH"]
        r = compile_ee(cmd, f, o, ee=False)
        return (o if r.returncode == 0 else None), f, r.stderr
    if kind == "gs":
        r = run(base + ["-O3", "-include", "r36s_force.h", "-include", "vita_cold.h", "-Dgetenv=Vita_GsGetenv"] + SHIM_INC +
                [f"-I{ROOT / 'port/src/gs'}", f"-I{V / 'sdlshim'}", str(f), "-o", str(o)])
    elif kind == "hard":
        if f.name in NEON_FILES and not USE_GCC and not A64:
            base = base + ["-mcpu=cortex_a35"]
        r = run(base + ["-O3", "-include", "r36s_force.h", "-include", "vita_cold.h"] + SHIM_INC + [f"-I{GEN}", str(f), "-o", str(o)])
    elif kind == "shim":
        r = run(base + ["-O2", "-D_GNU_SOURCE", "-Wall", "-Wno-unused-function"] + SHIM_INC + [str(f), "-o", str(o)])
    elif kind == "vu0":
        fast = ["-include", "vita_vu0_inline.h", "-DRefVu0_AddBits=VitaVu_AddBits", "-DRefVu0_MulBits=VitaVu_MulBits", "-DRefVu0_DivBits=VitaVu_DivBits"]
        if f.name == "vu0_b.c":
            fast += [f"-DRefVu0_{n}=VitaVuF_{n}" for n in ("Add", "Sub", "Mul", "Div")]
        r = rename(run(base + ABI + NOVEC + ["-O3"] + fast + ["-include", "math.h", "-include", "stdlib.h", "-include", "port_libm.h",
                                                            "-include", "vu0_names.h", "-DREF_VU0_EXTERN_ARITH", str(f), "-o", str(o)]), o)
    else:
        r = compile_ee(base + ABI + NOVEC + NOBUILTIN + ["-include", "math.h", "-include", "stdlib.h", "-include", "port_libm.h"], f, o)
    return (o if r.returncode == 0 else None), f, r.stderr


def cc_libm(f):
    o = OBJ / ("nl_" + f.stem + ".o")
    if fresh(o, f):
        return o, f, ""
    r = compile_ee(CC + ["-std=gnu89", "-c"] + COMMON + ABI + NOVEC + NOBUILTIN + ["-Iport/include", f"-I{f.parent}", "-include", "newlib_shim.h"], f, o, p32=False)
    return (o if r.returncode == 0 else None), f, r.stderr


def float_expected():
    """vita_float_expected.h: the reference sums of the float self-test (vita_float.c), computed on the host."""
    ren = [f"-D__{n}=PortX_{n}" for n in X_DEFS] + [f"-D{a}={b}" for a, b in RENAME_SOFTFLOAT.items()]
    cc = ["gcc", "-O2", "-w", "-Iinclude", "-Iport/include", "-Iport/vita"]
    for src, extra, o in (("port/src/softfloat_ps2.c", ren, "ft_soft.o"), ("src/port/vu0_a.c", ["-DREF_VU0_EXTERN_ARITH"], "ft_vu0.o"),
                          ("port/vita/floattest.c", [], "ft_main.o")):
        subprocess.run(cc + extra + ["-c", src, "-o", str(GEN / o)], cwd=ROOT, check=True)
    subprocess.run(["gcc", "-o", str(GEN / "floattest")] + [str(GEN / o) for o in ("ft_main.o", "ft_soft.o", "ft_vu0.o")] + ["-lm"], check=True)
    (GEN / "vita_float_expected.h").write_text(subprocess.run([str(GEN / "floattest")], check=True, capture_output=True, text=True).stdout)


def jobs():
    return int(os.environ.get("BT3_JOBS", "0")) or os.cpu_count() or 4


def compile_all(only=None):
    OBJ.mkdir(parents=True, exist_ok=True)
    GEN.mkdir(parents=True, exist_ok=True)
    float_expected()
    data = data_tables()
    with concurrent.futures.ProcessPoolExecutor(jobs()) as ex:
        res = []
        if only in (None, "game"):
            res += list(ex.map(cc_game, portsrc.sources()))
        if only in (None, "port"):
            res += list(ex.map(cc_libm, sorted((ROOT / "port/third_party/newlib_libm").glob("*.c"))))
            res += list(ex.map(cc_port, port_sources()))
    bad = [(f, e) for o, f, e in res if o is None]
    for f, e in bad:
        print("FAILED", f.name, first_error(e))
    if only is None:
        objs = [str(o) for o, _, _ in res if o] + data
        (OBJ / "objects.txt").write_text("\n".join(objs) + "\n")
    print(f"{len(res) - len(bad)} objects compiled, {len(bad)} failed")
    return not bad


FAST_WRAPS = "StgFrustum_TestBox StgFrustum_TestPart BtlTaskList_Update BtlTaskList_Draw EftGfx_DrawPolyAvgZFront".split()
VUFAST_WRAPS = """ClipPoly_ClipPlane ClipPoly_ProjectCur Mtx_Mul Mtx_MulVec3 Mtx_MulVec4 Mtx_RotateX Mtx_RotateXYZ Mtx_RotateY Mtx_RotateZ
    Mtx_RotateZXY Vec3_Add Vec3_Cross Vec3_Dist Vec3_DistSq Vec3_Dot Vec3_Length Vec3_LengthSq Vec3_Normalize Vec3_Scale Vec3_Sub Vec4_Add
    Vec4_Mul Vec4_Scale Vec4_Sub Vu0Cur_MulMtx Vu0Cur_MulMtxRev Vu0Cur_MulVec3 Vu0Cur_MulVec4 Vu0Cur_ProjectInt Vu0Cur_RotateX
    Vu0Cur_RotateXYZ Vu0Cur_RotateY Vu0Cur_RotateZ Vu0Cur_RotateZXY""".split()
PORT_WRAPS = "Progress_Main main exit Port_GsVif1Chain sceMpegGetPicture GsVu1_Call pthread_create".split()


def sdl_stub():
    """A libSDL2-2.0.so.0 to link against: every SDL_ name the objects use, as an empty function. The program runs
    with the system's own SDL2."""
    objs = (OBJ / "objects.txt").read_text().split()
    names = set()
    for o in objs:
        r = run([BU + "nm", "-u", o])
        names |= set(re.findall(r"\b(SDL_\w+)", r.stdout))
    src = GEN / "sdl_stub.c"
    src.write_text("".join(f"void {n}(void) {{}}\n" for n in sorted(names)))
    so = GEN / "libSDL2-2.0.so.0"
    r = run(CC + ["-shared", "-fPIC", "-Wl,-soname,libSDL2-2.0.so.0", str(src), "-o", str(so)])
    if r.returncode:
        raise SystemExit("SDL2 stub: " + r.stderr)
    return so


LIBGCC = next(iter(sorted(pathlib.Path("/usr/lib/gcc-cross/" + ("aarch64-linux-gnu" if A64 else "arm-linux-gnueabihf")).glob("*/libgcc.a"))), "")  # (apt: gcc-arm-linux-gnueabihf / gcc-aarch64-linux-gnu)


def link_cmd(objs, extra):
    wraps = [f"-Wl,--wrap={n}" for n in PORT_WRAPS + FAST_WRAPS + VUFAST_WRAPS]
    so = GEN / "libSDL2-2.0.so.0"
    # -rtlib=none: zig's own runtime brought its memcpy, memset, memcmp (bcmp a byte at a time) and strlen, which then
    # took the place of the C library's (a fifth of the list reader's time was its bcmp); the few helpers still needed
    # (64-bit division and conversions) come from GCC's libgcc.a, linked in (nothing more to carry to the device)
    return CC + ["-no-pie", "-w", "-rtlib=none", "-o", str(OUT / "bt3")] + extra + objs + [str(so), "-lpthread", "-lm", "-ldl", "-lrt"] + wraps + \
           ["-Wl,--allow-shlib-undefined", "-Wl,-rpath,$ORIGIN/libs", LIBGCC]


def link():
    OUT.mkdir(parents=True, exist_ok=True)
    objs = (OBJ / "objects.txt").read_text().split()
    sdl_stub()
    r = run(link_cmd(objs, []))
    need = sorted(set(re.findall(r"undefined symbol: (\w+)", r.stderr)))
    lines = ["/* Generated by port/r36s/build.py: one stub per symbol nothing provides. */", "extern void Port_Missing(const char *name);"]
    lines += [f'void {s}(void) {{ Port_Missing("{s}"); }}' for s in need]
    (GEN / "stubs.c").write_text("\n".join(lines) + "\n")
    so = OBJ / "pc_stubs.o"
    subprocess.run(CC + ["-c", "-O1", str(GEN / "stubs.c"), "-o", str(so)], check=True, cwd=ROOT)
    r = run(link_cmd(objs, [str(so)]))
    errs = [l for l in r.stderr.splitlines() if "duplicate symbol" in l or "undefined symbol" in l or "error" in l]
    print(f"{len(need)} stubs: {' '.join(need[:60])}{' ...' if len(need) > 60 else ''}")
    print(f"link {'OK' if r.returncode == 0 else 'FAILED'}")
    for l in errs[:30]:
        print("  " + l[-240:])
    return r.returncode == 0


def package():
    """The ArkOS / PortMaster layout: ports/Tenkaichi3.sh and ports/bt3/ (the program, its notes, the disc folder).
    The TrimUI Smart Pro (stock system, PortMaster): Roms/PORTS/Tenkaichi3.sh and Data/ports/bt3/."""
    pkg = OUT / "package"
    if pkg.exists():
        shutil.rmtree(pkg)
    game = pkg / ("Data/ports/bt3" if TSP else "ports/bt3_64" if A64 else "ports/bt3")
    (game / "disc").mkdir(parents=True)
    shutil.copy(OUT / "bt3", OUT / "bt3.unstripped")  # (for reading crash.txt)
    shutil.copy(OUT / "bt3", game / "bt3")
    subprocess.run([BU + "strip", "-g", str(game / "bt3")], check=True)
    # the functions' names for prof.txt (shim/r36s_prof.c): address and name, in address order
    nm = subprocess.run([BU + "nm", "-n", "-S", "--defined-only", str(OUT / "bt3")], check=True, capture_output=True, text=True).stdout
    sym = []
    for l in nm.splitlines():
        w = l.split(" ", 3)
        if len(w) == 3:
            w = [w[0], "0", w[1], w[2]]
        if len(w) == 4 and w[2] in "tTwW":
            sym.append(f"{w[0]} {int(w[1], 16):x} {w[3]}\n")
    (game / "bt3.sym").write_text("".join(sym))
    os.chmod(game / "bt3", 0o755)
    P = ROOT / "port/trimui/package" if TSP else R / "package"
    sh = pkg / ("Roms/PORTS" if TSP else "ports") / ("Dragon Ball Z Budokai Tenkaichi 3 (64-bit).sh" if A64 and not TSP else "Dragon Ball Z Budokai Tenkaichi 3.sh")
    sh.parent.mkdir(parents=True, exist_ok=True)
    REL = TSP and os.environ.get("BT3_RELEASE") == "1"  # (the release: the clean launcher, no measurements, no App folder)
    shutil.copy(P / ("Tenkaichi3_release.sh" if REL else "Tenkaichi3_64.sh" if A64 and not TSP else "Tenkaichi3.sh"), sh)
    os.chmod(sh, 0o755)
    shutil.copy(P / "README.txt", game / "README.txt")
    if TSP and not REL:  # (also as a TrimUI App: started without PortMaster)
        shutil.copytree(P / "app", pkg / "Apps/BT3")
        os.chmod(pkg / "Apps/BT3/launch.sh", 0o755)
    (game / "disc/PUT_YOUR_DISC_FILES_HERE.txt").write_text(
        "Copy SLUS_216.78 and the BIN and DATA folders of your own USA disc (SLUS-21678) here.\n"
        "Copia aqui SLUS_216.78 y las carpetas BIN y DATA de tu propio disco USA (SLUS-21678).\n")
    if TSP:  # (the notices first, then the AGPL's text; the MIT notice and the third-party list beside it)
        (game / "LICENSE.txt").write_text((P / "NOTICE.txt").read_text() + (ROOT / "LICENSE").read_text())
        shutil.copy(ROOT / "LICENSE-MIT", game / "LICENSE-MIT.txt")
        shutil.copy(ROOT / "THIRD_PARTY.md", game / "THIRD_PARTY.txt")
    else:
        shutil.copy(ROOT / "LICENSE", game / "LICENSE.txt")
    zipname = OUT / (("Tenkaichi3Decomp-TrimUI-release.zip" if REL else "Tenkaichi3Decomp-TrimUI.zip") if TSP else "Tenkaichi3Decomp-R36S-64.zip" if A64 else "Tenkaichi3Decomp-R36S.zip")
    if zipname.exists():
        zipname.unlink()
    subprocess.run(["zip", "-qr", str(zipname)] + ((["Roms", "Data"] if REL else ["Roms", "Data", "Apps"]) if TSP else ["ports"]), cwd=pkg, check=True)
    print(f"{zipname}: {zipname.stat().st_size} bytes")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    steps = args or ["compile", "link", "package"]
    if "game" in steps or "port" in steps:
        sys.exit(0 if compile_all(steps[0]) else 1)
    if "compile" in steps and not compile_all():
        sys.exit(1)
    if "link" in steps and not link():
        sys.exit(1)
    if "package" in steps:
        package()


if __name__ == "__main__":
    main()
