/*
 * R36S: the Vita renderer's Cg shaders (port/vita/vita_vgl.c) as GLSL ES 3.00 for the Mali GPU.
 *
 * vitaGL compiles Cg at run time; OpenGL ES takes GLSL. The shaders of vita_vgl.c use a small part of Cg, and this
 * translates that part, token by token:
 *   - types (float4 -> vec4, half4 -> vec4, float4x4 -> mat4), casts ((float)i -> float(i)), literals (0.5f -> 0.5);
 *   - functions (tex2D -> texture, tex2Dproj -> textureProj, frac -> fract, fmod -> mod, lerp -> mix, saturate);
 *   - main's parameters: uniforms become global uniforms, a vertex shader's inputs attributes (their names kept for
 *     glBindAttribLocation), outputs and inputs between the stages varyings named by their semantic (TEXCOORD0..),
 *     always a vec4 (the two stages may give them different sizes: Cg links by semantic, GLSL by name and type);
 *     POSITION is gl_Position, a fragment shader's result is an out vec4.
 * Every vertex shader also gets the uniform r36_flip: the display is drawn upside down, as vitaGL keeps it in memory
 * (row 0 at the top), so that the renderer's reads of the display as a texture find what they expect (r36s_gl.c).
 * Anything else (a construct this does not know) is copied as it is and shows up as a compile error in the log.
 */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { T_WS, T_ID, T_NUM, T_PUNCT, T_END };
typedef struct Tok { int kind; const char *s; int n; } Tok;

typedef struct Buf { char *p; int n, cap; } Buf;
static void b_put(Buf *b, const char *s, int n) {
    if (b->n + n + 1 > b->cap) {
        b->cap = (b->n + n + 1) * 2 + 256;
        b->p = (char *)realloc(b->p, (size_t)b->cap);
    }
    memcpy(b->p + b->n, s, (size_t)n);
    b->n += n;
    b->p[b->n] = 0;
}
static void b_str(Buf *b, const char *s) { b_put(b, s, (int)strlen(s)); }
static void b_fmt(Buf *b, const char *fmt, ...) {
    char tmp[512];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    b_put(b, tmp, n < (int)sizeof(tmp) ? n : (int)sizeof(tmp) - 1);
}

static int tokenize(const char *s, int len, Tok **out) {
    int cap = 256, n = 0, i = 0;
    Tok *t = (Tok *)malloc(sizeof(Tok) * (size_t)cap);
    while (i < len) {
        Tok k;
        int j = i;
        char c = s[i];
        if (c == '/' && i + 1 < len && s[i + 1] == '/') {
            while (j < len && s[j] != '\n') { j++; }
            k.kind = T_WS;
        } else if (c == '/' && i + 1 < len && s[i + 1] == '*') {
            j += 2;
            while (j + 1 < len && !(s[j] == '*' && s[j + 1] == '/')) { j++; }
            j = j + 2 > len ? len : j + 2;
            k.kind = T_WS;
        } else if (isspace((unsigned char)c)) {
            while (j < len && isspace((unsigned char)s[j])) { j++; }
            k.kind = T_WS;
        } else if (isalpha((unsigned char)c) || c == '_') {
            while (j < len && (isalnum((unsigned char)s[j]) || s[j] == '_')) { j++; }
            k.kind = T_ID;
        } else if (isdigit((unsigned char)c) || (c == '.' && i + 1 < len && isdigit((unsigned char)s[i + 1]))) {
            while (j < len && (isalnum((unsigned char)s[j]) || s[j] == '.' ||
                               ((s[j] == '-' || s[j] == '+') && (s[j - 1] == 'e' || s[j - 1] == 'E')))) { j++; }
            k.kind = T_NUM;
        } else {
            j++;
            k.kind = T_PUNCT;
        }
        k.s = s + i;
        k.n = j - i;
        if (n + 1 >= cap) {
            cap *= 2;
            t = (Tok *)realloc(t, sizeof(Tok) * (size_t)cap);
        }
        t[n++] = k;
        i = j;
    }
    t[n].kind = T_END;
    t[n].s = s + len;
    t[n].n = 0;
    *out = t;
    return n;
}

static int is(const Tok *t, const char *w) { return (int)strlen(w) == t->n && memcmp(t->s, w, (size_t)t->n) == 0; }
static int is_p(const Tok *t, char c) { return t->kind == T_PUNCT && t->s[0] == c; }

/* a Cg type name -> the GLSL one (NULL: not a type) */
static const char *map_type(const Tok *t) {
    static const struct { const char *cg, *gl; } m[] = {
        {"float", "float"}, {"half", "float"}, {"fixed", "float"}, {"int", "int"}, {"bool", "bool"}, {"void", "void"},
        {"float2", "vec2"}, {"float3", "vec3"}, {"float4", "vec4"}, {"half2", "vec2"}, {"half3", "vec3"}, {"half4", "vec4"},
        {"fixed2", "vec2"}, {"fixed3", "vec3"}, {"fixed4", "vec4"}, {"int2", "ivec2"}, {"int3", "ivec3"}, {"int4", "ivec4"},
        {"bool2", "bvec2"}, {"bool3", "bvec3"}, {"bool4", "bvec4"}, {"float2x2", "mat2"}, {"float3x3", "mat3"}, {"float4x4", "mat4"},
        {"half2x2", "mat2"}, {"half3x3", "mat3"}, {"half4x4", "mat4"}, {"sampler2D", "sampler2D"}};
    unsigned i;
    if (t->kind != T_ID) {
        return NULL;
    }
    for (i = 0; i < sizeof(m) / sizeof(m[0]); i++) {
        if (is(t, m[i].cg)) {
            return m[i].gl;
        }
    }
    return NULL;
}
static int type_size(const char *gl) { /* components of a float type: 1..4, 0 otherwise */
    if (strcmp(gl, "float") == 0) { return 1; }
    if (strcmp(gl, "vec2") == 0) { return 2; }
    if (strcmp(gl, "vec3") == 0) { return 3; }
    if (strcmp(gl, "vec4") == 0) { return 4; }
    return 0;
}
static const char *map_func(const Tok *t) {
    static const struct { const char *cg, *gl; } m[] = {
        {"tex2D", "texture"}, {"tex2Dproj", "textureProj"}, {"frac", "fract"}, {"fmod", "mod"}, {"lerp", "mix"},
        {"rsqrt", "inversesqrt"}, {"ddx", "dFdx"}, {"ddy", "dFdy"}, {"atan2", "atan"}};
    unsigned i;
    for (i = 0; i < sizeof(m) / sizeof(m[0]); i++) {
        if (is(t, m[i].cg)) {
            return m[i].gl;
        }
    }
    return NULL;
}
static int next_nws(const Tok *t, int i) {
    while (t[i].kind == T_WS) { i++; }
    return i;
}
/* index after the bracket that closes the one at i (i: '(' or '[') */
static int skip_group(const Tok *t, int i) {
    char open = t[i].s[0], close = open == '(' ? ')' : ']';
    int depth = 0;
    for (; t[i].kind != T_END; i++) {
        if (is_p(&t[i], open)) { depth++; }
        else if (is_p(&t[i], close) && --depth == 0) { return i + 1; }
    }
    return i;
}
/* the end of the operand of a cast that starts at i: a group, or a name / number with calls, members, indices */
static int operand_end(const Tok *t, int i) {
    i = next_nws(t, i);
    if (is_p(&t[i], '-') || is_p(&t[i], '+') || is_p(&t[i], '!')) {
        return operand_end(t, i + 1);
    }
    if (is_p(&t[i], '(')) {
        i = skip_group(t, i);
    } else if (t[i].kind == T_ID || t[i].kind == T_NUM) {
        i++;
    } else {
        return i;
    }
    for (;;) {
        int j = next_nws(t, i);
        if (is_p(&t[j], '(') || is_p(&t[j], '[')) {
            i = skip_group(t, j);
        } else if (is_p(&t[j], '.') && t[next_nws(t, j + 1)].kind == T_ID) {
            i = next_nws(t, j + 1) + 1;
        } else {
            return i;
        }
    }
}

/* tokens [a, b) translated: types, functions, casts, literals */
static void emit_range(Buf *o, const Tok *t, int a, int b) {
    int i = a;
    while (i < b) {
        const Tok *k = &t[i];
        if (is_p(k, '(')) { /* a cast: (type) operand */
            int j = next_nws(t, i + 1), e;
            const char *ty = map_type(&t[j]);
            if (ty != NULL && strcmp(ty, "void") != 0 && strcmp(ty, "sampler2D") != 0) {
                int c = next_nws(t, j + 1);
                if (is_p(&t[c], ')')) {
                    int s = next_nws(t, c + 1);
                    if (!is_p(&t[s], ')') && !is_p(&t[s], ';') && !is_p(&t[s], ',') && t[s].kind != T_END &&
                        (e = operand_end(t, c + 1)) > s && e <= b) {
                        b_str(o, ty);
                        b_str(o, "(");
                        emit_range(o, t, c + 1, e);
                        b_str(o, ")");
                        i = e;
                        continue;
                    }
                }
            }
            b_put(o, k->s, k->n);
            i++;
            continue;
        }
        if (k->kind == T_NUM) {
            int n = k->n;
            while (n > 1 && (k->s[n - 1] == 'f' || k->s[n - 1] == 'F' || k->s[n - 1] == 'h' || k->s[n - 1] == 'H') &&
                   !(k->s[0] == '0' && (k->s[1] == 'x' || k->s[1] == 'X'))) {
                n--;
            }
            b_put(o, k->s, n);
        } else if (k->kind == T_ID) {
            const char *ty = map_type(k), *fn = map_func(k);
            if (ty != NULL) {
                b_str(o, ty);
            } else if (fn != NULL) {
                b_str(o, fn);
            } else if (is(k, "static")) {
                /* (Cg's static locals: none that keep a value between calls here) */
            } else {
                b_put(o, k->s, k->n);
            }
        } else {
            b_put(o, k->s, k->n);
        }
        i++;
    }
}

typedef struct Param {
    int uniform, out;
    char type[16];      /* the GLSL type */
    char name[64];
    char arr[16];       /* "[n]" or "" */
    char sem[24];       /* the semantic, upper case, or "" */
} Param;

static void copy_tok(char *dst, int size, const Tok *t) {
    int n = t->n < size - 1 ? t->n : size - 1;
    memcpy(dst, t->s, (size_t)n);
    dst[n] = 0;
}

/* the varying of a semantic (TEXCOORD0, COLOR0...) */
static void var_name(char *dst, int size, const char *sem) {
    snprintf(dst, (size_t)size, "r36v_%s", strcmp(sem, "COLOR") == 0 ? "COLOR0" : sem);
}
static const char *widen(int size) { /* x -> vec4(x, ...) of a value of `size` components */
    return size == 4 ? "%s" : size == 3 ? "vec4(%s, 1.0)" : size == 2 ? "vec4(%s, 0.0, 1.0)" : "vec4(%s, 0.0, 0.0, 1.0)";
}
static const char *narrow(int size) {
    return size == 4 ? "" : size == 3 ? ".xyz" : size == 2 ? ".xy" : ".x";
}

/* The translation. vertex: 1 for a vertex shader. Returns a malloc'd GLSL source, or NULL with err filled. */
char *R36_CgToGlsl(const char *src, int len, int vertex, char *err, int errlen) {
    Tok *t;
    int n = tokenize(src, len < 0 ? (int)strlen(src) : len, &t), i, mainAt = -1, retAt = -1, pOpen, pClose, bOpen, bClose;
    Buf o = {0, 0, 0};
    Param ps[24];
    int np = 0, k, retSize = 0;
    char retType[16] = "";

    (void)n;
    /* main: <type> main ( params ) [: SEM] { body } at the top level */
    {
        int depth = 0;
        for (i = 0; t[i].kind != T_END; i++) {
            if (is_p(&t[i], '{')) { depth++; }
            else if (is_p(&t[i], '}')) { depth--; }
            else if (depth == 0 && t[i].kind == T_ID && is(&t[i], "main") && is_p(&t[next_nws(t, i + 1)], '(')) {
                mainAt = i;
                break;
            }
        }
    }
    if (mainAt < 0) {
        snprintf(err, (size_t)errlen, "r36 cg: no main");
        free(t);
        return NULL;
    }
    for (retAt = mainAt - 1; retAt >= 0 && t[retAt].kind == T_WS; retAt--) {
    }
    if (retAt < 0 || map_type(&t[retAt]) == NULL) {
        snprintf(err, (size_t)errlen, "r36 cg: main without a return type");
        free(t);
        return NULL;
    }
    snprintf(retType, sizeof(retType), "%s", map_type(&t[retAt]));
    retSize = type_size(retType);
    pOpen = next_nws(t, mainAt + 1);
    pClose = skip_group(t, pOpen) - 1;
    bOpen = next_nws(t, pClose + 1);
    if (is_p(&t[bOpen], ':')) { /* the result's semantic */
        bOpen = next_nws(t, next_nws(t, bOpen + 1) + 1);
    }
    if (!is_p(&t[bOpen], '{')) {
        snprintf(err, (size_t)errlen, "r36 cg: main's body not found");
        free(t);
        return NULL;
    }
    {
        int depth = 0;
        for (bClose = bOpen; t[bClose].kind != T_END; bClose++) {
            if (is_p(&t[bClose], '{')) { depth++; }
            else if (is_p(&t[bClose], '}') && --depth == 0) { break; }
        }
    }
    /* the parameters */
    for (i = pOpen + 1; i < pClose && np < 24;) {
        Param *p = &ps[np];
        int j, end, depth = 0;
        memset(p, 0, sizeof(*p));
        for (end = i; end < pClose; end++) {
            if (is_p(&t[end], '(') || is_p(&t[end], '[')) { depth++; }
            else if (is_p(&t[end], ')') || is_p(&t[end], ']')) { depth--; }
            else if (depth == 0 && is_p(&t[end], ',')) { break; }
        }
        for (j = i; j < end; j++) {
            const char *ty;
            if (t[j].kind == T_WS) {
                continue;
            }
            if (is(&t[j], "uniform")) { p->uniform = 1; }
            else if (is(&t[j], "out")) { p->out = 1; }
            else if (is(&t[j], "in") || is(&t[j], "const")) { }
            else if (is(&t[j], "inout")) { p->out = 1; }
            else if ((ty = map_type(&t[j])) != NULL && p->type[0] == 0) { snprintf(p->type, sizeof(p->type), "%s", ty); }
            else if (t[j].kind == T_ID && p->name[0] == 0) { copy_tok(p->name, sizeof(p->name), &t[j]); }
            else if (is_p(&t[j], '[')) {
                int e = skip_group(t, j), q = 0, m;
                for (m = j; m < e && q < (int)sizeof(p->arr) - 1; m++) {
                    int c;
                    for (c = 0; c < t[m].n && q < (int)sizeof(p->arr) - 1; c++) {
                        p->arr[q++] = t[m].s[c];
                    }
                }
                p->arr[q] = 0;
                j = e - 1;
            } else if (is_p(&t[j], ':')) {
                int s = next_nws(t, j + 1);
                copy_tok(p->sem, sizeof(p->sem), &t[s]);
                for (char *c = p->sem; *c; c++) { *c = (char)toupper((unsigned char)*c); }
                j = s;
            }
        }
        if (p->type[0] && p->name[0]) {
            np++;
        }
        i = end + 1;
    }

    {   /* (R36_FRAG_MP=1, the R36S's launcher: the pixel shaders in medium precision, the Mali's half-float rate) */
        static int mp = -1;
        if (mp < 0) {
            const char *e = getenv("R36_FRAG_MP");
            mp = e != NULL && atoi(e) > 0;
        }
        b_str(&o, !vertex && mp ? "#version 300 es\nprecision mediump float;\nprecision highp int;\nprecision mediump sampler2D;\n"
                                : "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision mediump sampler2D;\n");
    }
    b_str(&o, "#define saturate(x) clamp((x), 0.0, 1.0)\n");
    if (vertex) {
        b_str(&o, "uniform float r36_flip;\n");
    }
    for (k = 0; k < np; k++) {
        Param *p = &ps[k];
        char vn[40];
        if (p->uniform) {
            b_fmt(&o, "uniform %s %s%s;\n", p->type, p->name, p->arr);
        } else if (vertex && !p->out) {
            b_fmt(&o, "in %s %s;\n", p->type, p->name);
        } else if (strcmp(p->sem, "POSITION") == 0 || strcmp(p->sem, "WPOS") == 0) {
        } else if (vertex || !p->out) {
            var_name(vn, sizeof(vn), p->sem[0] ? p->sem : p->name);
            b_fmt(&o, "%s vec4 %s;\n", vertex ? "out" : "in", vn);
        } else {
            b_fmt(&o, "out vec4 r36_out_%s;\n", p->name);
        }
    }
    if (!vertex && retSize > 0) {
        b_str(&o, "out vec4 r36_FragColor;\n");
    }
    /* everything before main (globals, helpers), translated */
    emit_range(&o, t, 0, retAt);
    b_str(&o, "void main() {\n");
    /* main's locals for its parameters */
    for (k = 0; k < np; k++) {
        Param *p = &ps[k];
        char vn[40];
        if (p->uniform || (vertex && !p->out)) {
            continue;
        }
        if (!vertex && !p->out && (strcmp(p->sem, "WPOS") == 0 || strcmp(p->sem, "VPOS") == 0)) {
            b_fmt(&o, "    %s %s = %s(gl_FragCoord%s);\n", p->type, p->name, p->type, narrow(type_size(p->type)));
        } else if (!vertex && !p->out) {
            var_name(vn, sizeof(vn), p->sem[0] ? p->sem : p->name);
            b_fmt(&o, "    %s %s = %s(%s%s);\n", p->type, p->name, p->type, vn, narrow(type_size(p->type)));
        } else {
            b_fmt(&o, "    %s %s;\n", p->type, p->name);
        }
    }
    /* the body; each return writes the outputs first */
    {
        Buf epi = {0, 0, 0};
        for (k = 0; k < np; k++) {
            Param *p = &ps[k];
            char vn[40], w[96];
            if (!p->out) {
                continue;
            }
            if (vertex && strcmp(p->sem, "POSITION") == 0) {
                b_fmt(&epi, " gl_Position = %s; gl_Position.y *= r36_flip;", p->name);
            } else if (vertex) {
                var_name(vn, sizeof(vn), p->sem[0] ? p->sem : p->name);
                snprintf(w, sizeof(w), widen(type_size(p->type)), p->name);
                b_fmt(&epi, " %s = %s;", vn, w);
            } else {
                snprintf(w, sizeof(w), widen(type_size(p->type)), p->name);
                b_fmt(&epi, " r36_out_%s = %s;", p->name, w);
            }
        }
        for (i = bOpen + 1; i < bClose;) {
            if (t[i].kind == T_ID && is(&t[i], "return")) {
                int e = i + 1, depth = 0;
                for (; e < bClose; e++) {
                    if (is_p(&t[e], '(')) { depth++; }
                    else if (is_p(&t[e], ')')) { depth--; }
                    else if (depth == 0 && is_p(&t[e], ';')) { break; }
                }
                b_str(&o, "{");
                if (!vertex && retSize > 0) {
                    char w[32];
                    snprintf(w, sizeof(w), "%s", widen(retSize));
                    b_str(&o, " r36_FragColor = ");
                    if (retSize == 4) {
                        emit_range(&o, t, i + 1, e);
                    } else {
                        Buf x = {0, 0, 0};
                        emit_range(&x, t, i + 1, e);
                        b_fmt(&o, w, x.p ? x.p : "0.0");
                        free(x.p);
                    }
                    b_str(&o, ";");
                }
                if (epi.p) {
                    b_str(&o, epi.p);
                }
                b_str(&o, " return; }");
                i = e + 1;
                continue;
            }
            {   /* the stretch up to the next return */
                int e = i;
                while (e < bClose && !(t[e].kind == T_ID && is(&t[e], "return"))) { e++; }
                emit_range(&o, t, i, e);
                i = e;
            }
        }
        if (epi.p) {
            b_str(&o, "\n   ");
            b_str(&o, epi.p);
        }
        b_str(&o, "\n}\n");
        free(epi.p);
    }
    /* anything after main */
    if (t[bClose].kind != T_END) {
        emit_range(&o, t, bClose + 1, n);
    }
    free(t);
    return o.p;
}
