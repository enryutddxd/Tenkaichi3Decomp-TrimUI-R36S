#!/usr/bin/env python3
"""Checks the Cg -> GLSL ES translation (port/r36s/shim/r36s_cg.c) on every shader of port/vita/vita_vgl.c.

The fixed shaders are read from vita_vgl.c's string constants; the pixel shader variants are made by vita_vgl.c's
own frag_variant() (compiled on the host with a few stand-ins), for every variant the renderer builds. Each one is
translated and handed to glslangValidator (the Khronos reference compiler) as GLSL ES 3.00; the vertex and pixel
shaders the renderer pairs are also linked together.

    python3 port/r36s/tests/shader_check.py [-v]
"""
import json, os, pathlib, re, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).resolve().parents[3]
VGL = (ROOT / "port/vita/vita_vgl.c").read_text(encoding="latin-1")
OUT = ROOT / "port/build/shader_check"
OUT.mkdir(parents=True, exist_ok=True)
VERBOSE = "-v" in sys.argv


def c_strings(name):
    """the value of `static const char name[] = "..." "...";`"""
    m = re.search(r"static const char " + name + r"\[\] *=(.*?);\n", VGL, re.S)
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    out = ""
    for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', body):
        out += bytes(lit, "latin-1").decode("unicode_escape")
    return out


def func_text(name):
    """a static function of vita_vgl.c, whole"""
    m = re.search(r"\n(static [^\n]*\b" + name + r"\([^\n]*\{\n.*?\n\}\n)", VGL, re.S)
    return m.group(1)


VERTS = ["kVertFt", "kVertStage", "kVertFtD", "kVertStD", "kVert", "kVertNP", "kVertM", "kVertVu", "kVertOl", "kVertFx"]
FRAGS = ["kFrag", "kFragOl", "kFragCopy", "kFragTblIds", "kFragTblFog", "kFragFogCopy", "kFragXfade", "kFragTblByte",
         "kFragGlowDown", "kFragGlowBlur", "kFragGlowAdd"]

# the host program: the translator, and frag_variant with what it calls stood in
harness = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef unsigned GLuint; typedef int GLint; typedef int GLsizei; typedef unsigned GLenum;
#define GL_CG_FRAGMENT_SHADER_EXT 1
char *R36_CgToGlsl(const char *src, int len, int vertex, char *err, int errlen);
static int gVglSafe, gVglSafe2;
static int sNoProj = 1, sVsNP = 1, sVsM = 1;
static void Vita_Log(const char *f, ...) { (void)f; }
static unsigned sNext = 1;
static const char *sSrc[4096];
static GLuint glCreateShader(GLenum t) { (void)t; return sNext++; }
static void glGetShaderInfoLog(GLuint s, int n, void *l, char *b) { (void)s; (void)n; (void)l; b[0] = 0; }
static void vglGetShaderBinary(GLuint s, GLsizei n, GLsizei *l, void *b) { (void)s; (void)n; (void)b; *l = 0; }
static int sh_cached(GLuint sh, const char *src, GLint len) { sSrc[sh] = strndup(src, (size_t)len); return 1; }
static GLuint sFragVar[2048];
#include <ctype.h>
#define DPB_FT_SLOT 24
#define DPB_FT_K 16
#define DPB_ST_SLOT 7
#define DPB_ST_K 32
''' + func_text("frag_variant") + func_text("var_used") + func_text("var_used_k") + func_text("dp_batch_src") + \
    "static const char kFtD[] = " + json.dumps(c_strings("kVertFtD")) + ";\nstatic const char kStD[] = " + json.dumps(c_strings("kVertStD")) + ";\n" + r'''
int main(int argc, char **argv) {
    int kk, ff;
    static int done[2048];
    (void)argc;
    {   /* the R36S's batched direct shaders, made as vita_vgl.c makes them */
        char path[300];
        FILE *f;
        snprintf(path, sizeof(path), "%s/kVertFtB.cg", argv[1]);
        f = fopen(path, "w"); fputs(dp_batch_src(kFtD, DPB_FT_SLOT, DPB_FT_K, 23), f); fclose(f);
        snprintf(path, sizeof(path), "%s/kVertStB.cg", argv[1]);
        f = fopen(path, "w"); fputs(dp_batch_src(kStD, DPB_ST_SLOT, DPB_ST_K, 6), f); fclose(f);
    }
    for (kk = 0; kk < 5; kk++) {
        for (ff = 0; ff < 2048; ff++) {
            if (var_used_k(kk, ff) && !done[ff]) {
                GLuint fs = frag_variant(ff);
                char path[300];
                FILE *f;
                done[ff] = 1;
                snprintf(path, sizeof(path), "%s/variant_%04d.cg", argv[1], ff);
                f = fopen(path, "w");
                fputs(sSrc[fs], f);
                fclose(f);
            }
        }
    }
    return 0;
}
'''
(OUT / "harness.c").write_text(harness, encoding="latin-1")
tr = r'''
#include <stdio.h>
#include <stdlib.h>
char *R36_CgToGlsl(const char *src, int len, int vertex, char *err, int errlen);
int main(int argc, char **argv) {
    static char buf[1 << 16], err[300];
    FILE *f = fopen(argv[2], "rb");
    int n = (int)fread(buf, 1, sizeof(buf) - 1, f);
    char *o;
    fclose(f);
    buf[n] = 0;
    o = R36_CgToGlsl(buf, n, argv[1][0] == 'v', err, sizeof(err));
    if (!o) { fprintf(stderr, "%s\n", err); return 1; }
    fputs(o, stdout);
    return 0;
}
'''
(OUT / "tr.c").write_text(tr)
cg = ROOT / "port/r36s/shim/r36s_cg.c"
subprocess.run(["gcc", "-O1", "-w", "-o", str(OUT / "harness"), str(OUT / "harness.c"), str(cg)], check=True)
subprocess.run(["gcc", "-O1", "-w", "-o", str(OUT / "tr"), str(OUT / "tr.c"), str(cg)], check=True)
subprocess.run([str(OUT / "harness"), str(OUT)], check=True)

shaders = [(n, "v", c_strings(n)) for n in VERTS] + [(n, "f", c_strings(n)) for n in FRAGS]
shaders += [(n, "v", (OUT / f"{n}.cg").read_text(encoding="latin-1")) for n in ("kVertFtB", "kVertStB")]
shaders += [(p.stem, "f", p.read_text(encoding="latin-1")) for p in sorted(OUT.glob("variant_*.cg"))]
bad = 0
glsl = {}
for name, kind, src in shaders:
    (OUT / f"{name}.cg").write_text(src, encoding="latin-1")
    r = subprocess.run([str(OUT / "tr"), kind, str(OUT / f"{name}.cg")], capture_output=True, text=True)
    if r.returncode:
        print(f"{name}: not translated: {r.stderr.strip()}")
        bad += 1
        continue
    ext = ".vert" if kind == "v" else ".frag"
    p = OUT / (name + ext)
    p.write_text(r.stdout)
    glsl[name] = p
    v = subprocess.run(["glslangValidator", str(p)], capture_output=True, text=True)
    if v.returncode:
        bad += 1
        print(f"{name}: GLSL errors:\n" + "\n".join("    " + l for l in v.stdout.splitlines() if "ERROR" in l))
        if VERBOSE:
            print(r.stdout)
    elif VERBOSE:
        print(f"{name}: ok")

# the pairs the renderer links (a vertex shader with the pixel shaders it is drawn with)
pairs = [("kVert", "kFrag"), ("kVertStage", "kFrag"), ("kVertFt", "kFrag"), ("kVertVu", "kFrag"), ("kVertOl", "kFragOl"),
         ("kVertFx", "kFragCopy"), ("kVertFx", "kFragTblIds"), ("kVertFx", "kFragGlowAdd")]
for v in ("kVert", "kVertNP", "kVertM", "kVertStage", "kVertFt", "kVertFtD", "kVertStD", "kVertFtB", "kVertStB"):
    for f in sorted(OUT.glob("variant_*.frag")):
        pairs.append((v, f.stem))
linked = 0
for v, f in pairs:
    if v in glsl and f in glsl:
        r = subprocess.run(["glslangValidator", "-l", str(glsl[v]), str(glsl[f])], capture_output=True, text=True)
        linked += 1
        if r.returncode:
            bad += 1
            print(f"link {v} + {f}:\n" + "\n".join("    " + l for l in r.stdout.splitlines() if "ERROR" in l))
print(f"{len(shaders)} shaders ({len(list(OUT.glob('variant_*.cg')))} pixel shader variants), {linked} pairs linked: {bad} problems")
sys.exit(1 if bad else 0)
