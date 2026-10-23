/*
 * R36S: the Vita's text screen (vitasdk's debugScreen, which port/vita/vita_main.c prints its messages on) for the
 * R36S: 80 x 60 characters of the PSP SDK's 8 x 8 font on the 640 x 480 screen, put up through r36s_gl.c whenever
 * something is printed while it is the display. The colours of the escapes vita_main.c uses (\e[31m ... \e[39m),
 * \e[2J\e[H (clear), \e[K (the rest of the line).
 *
 * sceDisplayGetFrameBuf / sceDisplaySetFrameBuf are here too: the renderer remembers the text screen's buffer at its
 * start and puts it back for a message at the end (Vita_TextScreen).
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "psp2_shim.h"
#include "r36s_font.h"

#define TW 640
#define TH 480
static uint32_t sPx[TW * TH];
static int sX, sY;
static uint32_t sFg = 0xFFFFFFFFu, sBg = 0xFF000000u;
int gR36TextOn = 1; /* the text screen is what is shown (0 once the renderer puts up a picture) */
extern int R36_VideoInit(void);
extern void R36_InputInit(void);
extern void R36_TextPresent(const uint32_t *px, int w, int h);

static uint32_t rgb(unsigned r, unsigned g, unsigned b) { return 0xFF000000u | b << 16 | g << 8 | r; }
static void newline(void) {
    sX = 0;
    sY += 8;
    if (sY + 8 > TH) { /* scrolled up a line */
        memmove(sPx, sPx + TW * 8, sizeof(uint32_t) * TW * (TH - 8));
        for (int i = TW * (TH - 8); i < TW * TH; i++) {
            sPx[i] = sBg;
        }
        sY = TH - 8;
    }
}
static void glyph(unsigned char c) {
    const unsigned char *g = &kR36Font[c * 8];
    int r, k;
    if (sX + 8 > TW) {
        newline();
    }
    for (r = 0; r < 8; r++) {
        uint32_t *o = &sPx[(sY + r) * TW + sX];
        for (k = 0; k < 8; k++) {
            o[k] = (g[r] & (0x80 >> k)) ? sFg : sBg;
        }
    }
    sX += 8;
}
int psvDebugScreenPuts(const char *s) {
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '\e' && s[1] == '[') { /* an escape: \e[ numbers ; ... letter */
            int args[4] = {0, 0, 0, 0}, n = 0;
            s += 2;
            while (*s && (*s == ';' || (*s >= '0' && *s <= '9'))) {
                if (*s == ';') {
                    n = n < 3 ? n + 1 : 3;
                } else {
                    args[n] = args[n] * 10 + (*s - '0');
                }
                s++;
            }
            if (*s == 'm') {
                for (int i = 0; i <= n; i++) {
                    int a = args[i];
                    static const unsigned kc[8][3] = {{0, 0, 0}, {255, 80, 80}, {90, 230, 90}, {240, 220, 80}, {100, 140, 255}, {230, 100, 230}, {90, 220, 230}, {255, 255, 255}};
                    if (a == 0 || a == 39) { sFg = 0xFFFFFFFFu; }
                    else if (a >= 30 && a <= 37) { sFg = rgb(kc[a - 30][0], kc[a - 30][1], kc[a - 30][2]); }
                    else if (a == 49) { sBg = 0xFF000000u; }
                }
            } else if (*s == 'J') {
                for (int i = 0; i < TW * TH; i++) { sPx[i] = sBg; }
            } else if (*s == 'H') {
                sX = args[1] > 0 ? (args[1] - 1) * 8 : 0;
                sY = args[0] > 0 ? (args[0] - 1) * 8 : 0;
            } else if (*s == 'K') {
                for (int r = 0; r < 8; r++) {
                    for (int x = sX; x < TW; x++) { sPx[(sY + r) * TW + x] = sBg; }
                }
            }
            if (*s == 0) {
                break;
            }
            continue;
        }
        if (c == '\n') {
            newline();
        } else if (c == '\r') {
            sX = 0;
        } else if (c == '\t') {
            do { glyph(' '); } while ((sX / 8) % 8 != 0);
        } else {
            glyph(c);
        }
    }
    if (gR36TextOn) {
        R36_TextPresent(sPx, TW, TH);
    }
    fputs("", stdout);
    return 0;
}
int psvDebugScreenPrintf(const char *fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fputs(buf, stdout); /* (and in out.txt) */
    return psvDebugScreenPuts(buf);
}
int psvDebugScreenInit(void) {
    for (int i = 0; i < TW * TH; i++) {
        sPx[i] = sBg;
    }
    R36_VideoInit();
    R36_InputInit();
    gR36TextOn = 1;
    return 0;
}
int psvDebugScreenFinish(void) { return 0; }

int sceDisplayGetFrameBuf(SceDisplayFrameBuf *p, SceDisplaySetBufSync sync) {
    (void)sync;
    memset(p, 0, sizeof(*p));
    p->size = sizeof(*p);
    p->base = gR36TextOn ? (void *)sPx : NULL;
    p->pitch = TW;
    p->width = TW;
    p->height = TH;
    return gR36TextOn ? 0 : -1;
}
int sceDisplaySetFrameBuf(const SceDisplayFrameBuf *p, SceDisplaySetBufSync sync) {
    (void)sync;
    if (p != NULL && p->base == (void *)sPx) {
        gR36TextOn = 1;
        R36_TextPresent(sPx, TW, TH);
    }
    return 0;
}
